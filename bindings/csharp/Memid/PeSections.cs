using System.Buffers.Binary;
using System.Text;

namespace Memid;

/// <summary>
/// Adds, replaces and removes a read-only data section in a PE image (an EXE or DLL as bytes), for tools that patch a
/// binary they didn't build: a <c>.memidx</c> describing the patch, or a <c>.memid</c> in a binary of one's own.
/// <para>
/// Only the <b>last</b> section can be replaced or removed, and a new section goes last. A tool that grows its own
/// last section (a code cave) removes the MEMID section first, patches, then sets it again.
/// </para>
/// <para>
/// Refused, with <see cref="InvalidOperationException"/>: a signed image (its certificate table would no longer
/// match), data after the last section (an overlay the new section would land behind), and a section table with no
/// room for another header.
/// </para>
/// </summary>
public static class PeSections
{
    /// <summary>Initialized data, readable: what a MEMID section is.</summary>
    public const uint ReadOnlyData = 0x40000040;

    /// <summary>The bytes of section <paramref name="name"/>, or null when the image has none.</summary>
    public static byte[]? Read(byte[] pe, string name)
    {
        var img = new Image(pe);
        int i = img.Find(name);
        if (i < 0) return null;
        var (_, vsize, _, raw, rawOffset) = img.Section(i);
        uint n = vsize != 0 ? Math.Min(vsize, raw) : raw;
        return pe.AsSpan((int)rawOffset, (int)n).ToArray();
    }

    /// <summary>The image with section <paramref name="name"/> holding <paramref name="data"/>: replaced when it is the
    /// last section, added after the last one when the image has none.</summary>
    public static byte[] Set(byte[] pe, string name, byte[] data, uint characteristics = ReadOnlyData)
    {
        byte[] without = Remove(pe, name);
        return Add(without, name, data, characteristics);
    }

    /// <summary>The image without section <paramref name="name"/> (unchanged when it has none).</summary>
    /// <exception cref="InvalidOperationException">The section isn't the last one.</exception>
    public static byte[] Remove(byte[] pe, string name)
    {
        var img = new Image(pe);
        int i = img.Find(name);
        if (i < 0) return pe;
        if (i != img.Count - 1) throw new InvalidOperationException($"{name} isn't the last section: only the last can be removed");
        var (_, _, _, raw, rawOffset) = img.Section(i);
        img.RefuseSignedOrOverlay();
        byte[] o = pe.AsSpan(0, (int)rawOffset).ToArray();
        o.AsSpan(img.Header(i), 40).Clear();
        BinaryPrimitives.WriteUInt16LittleEndian(o.AsSpan(img.Coff + 2), (ushort)(img.Count - 1));
        Span<byte> opt = o.AsSpan(img.Optional);
        BinaryPrimitives.WriteUInt32LittleEndian(opt[8..], BinaryPrimitives.ReadUInt32LittleEndian(opt[8..]) - raw);
        uint end = 0;
        for (int s = 0; s < img.Count - 1; s++)
        {
            var (va, vsize, _, sraw, _) = img.Section(s);
            end = Math.Max(end, va + Math.Max(vsize, sraw));
        }
        BinaryPrimitives.WriteUInt32LittleEndian(opt[56..], Align(Math.Max(end, img.SizeOfHeaders), img.SectionAlignment));
        return o;
    }

    /// <summary>The image with a new last section <paramref name="name"/> holding <paramref name="data"/>.</summary>
    /// <exception cref="InvalidOperationException">The image already has that section, is signed, has an overlay, or
    /// has no room for another section header.</exception>
    public static byte[] Add(byte[] pe, string name, byte[] data, uint characteristics = ReadOnlyData)
    {
        byte[] ascii = Encoding.ASCII.GetBytes(name);
        if (ascii.Length is 0 or > 8) throw new ArgumentException("a section name is 1 to 8 ASCII characters", nameof(name));
        var img = new Image(pe);
        if (img.Find(name) >= 0) throw new InvalidOperationException($"the image already has a {name} section");
        img.RefuseSignedOrOverlay();
        int slot = img.Header(img.Count);
        uint firstData = img.SizeOfHeaders;
        uint end = 0;
        for (int s = 0; s < img.Count; s++)
        {
            var (va, vsize, _, raw, rawOffset) = img.Section(s);
            if (raw > 0) firstData = Math.Min(firstData, rawOffset);
            end = Math.Max(end, va + Math.Max(vsize, raw));
        }
        if (slot + 40 > firstData || pe.AsSpan(slot, 40).IndexOfAnyExcept((byte)0) >= 0)
            throw new InvalidOperationException("no room in the section table for another section header");

        uint virtualAddress = Align(end, img.SectionAlignment);
        uint rawSize = Align((uint)data.Length, img.FileAlignment);
        uint rawOffsetNew = Align((uint)pe.Length, img.FileAlignment);
        byte[] o = new byte[rawOffsetNew + rawSize];
        pe.CopyTo(o, 0);
        data.CopyTo(o, (int)rawOffsetNew);

        Span<byte> h = o.AsSpan(slot, 40);
        ascii.CopyTo(h);
        BinaryPrimitives.WriteUInt32LittleEndian(h[8..], (uint)data.Length);  // VirtualSize
        BinaryPrimitives.WriteUInt32LittleEndian(h[12..], virtualAddress);
        BinaryPrimitives.WriteUInt32LittleEndian(h[16..], rawSize);
        BinaryPrimitives.WriteUInt32LittleEndian(h[20..], rawOffsetNew);
        BinaryPrimitives.WriteUInt32LittleEndian(h[36..], characteristics);
        BinaryPrimitives.WriteUInt16LittleEndian(o.AsSpan(img.Coff + 2), (ushort)(img.Count + 1));
        Span<byte> opt = o.AsSpan(img.Optional);
        if ((characteristics & 0x40) != 0)
            BinaryPrimitives.WriteUInt32LittleEndian(opt[8..], BinaryPrimitives.ReadUInt32LittleEndian(opt[8..]) + rawSize);
        BinaryPrimitives.WriteUInt32LittleEndian(opt[56..], Align(virtualAddress + (uint)Math.Max(data.Length, 1), img.SectionAlignment));
        return o;
    }

    static uint Align(uint v, uint a) => a == 0 ? v : (v + a - 1) / a * a;

    // The headers this needs, read with bounds checks
    sealed class Image
    {
        readonly byte[] pe;
        public readonly int Coff, Optional, Table, Count;
        public readonly uint SectionAlignment, FileAlignment, SizeOfHeaders;
        readonly int dataDirectories;
        readonly uint directoryCount;

        public Image(byte[] bytes)
        {
            pe = bytes;
            if (pe.Length < 0x40 || pe[0] != 'M' || pe[1] != 'Z') throw new InvalidDataException("not a PE image (no MZ header)");
            int nt = (int)U32(0x3C);
            if (nt < 0 || nt + 24 > pe.Length || U32(nt) != 0x00004550) throw new InvalidDataException("not a PE image (no PE header)");
            Coff = nt + 4;
            Count = U16(Coff + 2);
            int optionalSize = U16(Coff + 16);
            Optional = Coff + 20;
            Table = Optional + optionalSize;
            ushort magic = U16(Optional);
            dataDirectories = magic switch { 0x10B => 96, 0x20B => 112, _ => throw new InvalidDataException("unknown optional header") };
            if (optionalSize < dataDirectories || Table + 40L * Count > pe.Length) throw new InvalidDataException("truncated PE headers");
            SectionAlignment = U32(Optional + 32);
            FileAlignment = U32(Optional + 36);
            SizeOfHeaders = U32(Optional + 60);
            directoryCount = U32(Optional + dataDirectories - 4);
        }

        public int Header(int index) => Table + 40 * index;

        public (uint va, uint vsize, string name, uint raw, uint rawOffset) Section(int i)
        {
            int h = Header(i);
            string name = Encoding.ASCII.GetString(pe, h, 8).TrimEnd('\0');
            return (U32(h + 12), U32(h + 8), name, U32(h + 16), U32(h + 20));
        }

        public int Find(string name)
        {
            for (int i = 0; i < Count; i++)
                if (Section(i).name == name) return i;
            return -1;
        }

        public void RefuseSignedOrOverlay()
        {
            if (directoryCount > 4 && U32(Optional + dataDirectories + 4 * 8 + 4) != 0)
                throw new InvalidOperationException("the image is signed: sign it again after changing it, and change it before signing");
            long end = 0;
            for (int i = 0; i < Count; i++)
            {
                var (_, _, _, raw, rawOffset) = Section(i);
                end = Math.Max(end, (long)rawOffset + raw);
            }
            if (end < pe.Length) throw new InvalidOperationException("the image has data after its last section (an overlay)");
        }

        ushort U16(int at) => BinaryPrimitives.ReadUInt16LittleEndian(pe.AsSpan(at));
        uint U32(int at) => BinaryPrimitives.ReadUInt32LittleEndian(pe.AsSpan(at));
    }
}
