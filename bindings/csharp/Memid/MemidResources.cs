using System.Buffers.Binary;
using System.Text;

namespace Memid;

/// <summary>
/// A Win32 resource file (<c>.res</c>) holding MEMID data as RT_RCDATA resources named <c>MEMID</c> and
/// <c>MEMIDX</c>, which MEMID readers look for in a PE that has no <c>.memid</c> / <c>.memidx</c> section. For
/// binaries that can't get a section of their own, such as .NET apps: write the file before the build and reference it
/// from the project (<c>&lt;Win32Resource&gt;</c>). It replaces the resources the SDK would generate (version, icon,
/// manifest), so a project that needs those adds them to the same file with its own resource compiler.
/// </summary>
public static class MemidResources
{
    const ushort RtRcData = 10;

    /// <summary>The <c>.res</c> bytes: the identity and, when given, the custom metadata.</summary>
    public static byte[] WriteRes(byte[] identity, byte[]? custom = null)
    {
        using var ms = new MemoryStream();
        // A .res file starts with an empty resource entry
        ms.Write([0, 0, 0, 0, 0x20, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
        Entry(ms, "MEMID", identity);
        if (custom is { Length: > 0 }) Entry(ms, "MEMIDX", custom);
        return ms.ToArray();
    }

    // RESOURCEHEADER: DataSize, HeaderSize, TYPE (0xFFFF ordinal), NAME (UTF-16, NUL), DWORD padding, DataVersion,
    // MemoryFlags, LanguageId, Version, Characteristics; then the data, padded to a DWORD
    static void Entry(Stream s, string name, byte[] data)
    {
        var h = new List<byte>();
        void U16(ushort v) { Span<byte> b = stackalloc byte[2]; BinaryPrimitives.WriteUInt16LittleEndian(b, v); h.AddRange(b.ToArray()); }
        void U32(uint v) { Span<byte> b = stackalloc byte[4]; BinaryPrimitives.WriteUInt32LittleEndian(b, v); h.AddRange(b.ToArray()); }
        U32((uint)data.Length);
        U32(0);                             // HeaderSize, filled in below
        U16(0xFFFF); U16(RtRcData);
        h.AddRange(Encoding.Unicode.GetBytes(name + "\0"));
        while (h.Count % 4 != 0) h.Add(0);
        U32(0);                             // DataVersion
        U16(0x0030);                        // MemoryFlags: MOVEABLE | PURE
        U16(0);                             // LanguageId: neutral
        U32(0); U32(0);                     // Version, Characteristics
        byte[] header = [.. h];
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(4), (uint)header.Length);
        s.Write(header);
        s.Write(data);
        for (int pad = data.Length; pad % 4 != 0; pad++) s.WriteByte(0);
    }
}
