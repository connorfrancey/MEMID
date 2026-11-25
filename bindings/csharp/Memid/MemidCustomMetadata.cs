using System.Buffers.Binary;
using System.Text;

namespace Memid;

/// <summary>Payload format of a custom metadata entry.</summary>
public enum MetadataFormat : ushort { Raw = 0, Text = 1, Json = 2, Xml = 3, Cbor = 4, MessagePack = 5, Toml = 6 }

/// <summary>One custom metadata entry: a reverse-domain namespace and its payload.</summary>
public sealed record MemidEntry(string Namespace, ushort Format, byte[] Payload, ushort Flags = 0);

/// <summary>
/// A <c>.memidx</c> container (docs/CUSTOM_METADATA.md): developer metadata entries. <see cref="Serialize"/> writes it
/// as the C++ <c>memid::CustomMetadata::serialize</c> does; put the result in a <c>.memidx</c> section or a
/// <c>MEMIDX</c> RT_RCDATA resource. Several containers may follow one another in a section.
/// </summary>
public sealed class MemidCustomMetadata
{
    /// <summary>The container's magic: "MEMIDX" CR LF.</summary>
    public static ReadOnlySpan<byte> Magic => "MEMIDX\r\n"u8;

    const int HeaderSize = 24, EntryHeaderSize = 12;

    /// <summary>The entries, in the order they are written.</summary>
    public List<MemidEntry> Entries { get; } = [];

    /// <summary>Adds an entry. </summary>
    /// <exception cref="ArgumentException">A namespace that isn't 1-128 bytes of reverse-domain text.</exception>
    public MemidCustomMetadata Add(string ns, MetadataFormat format, byte[] payload)
    {
        int n = Encoding.UTF8.GetByteCount(ns);
        if (n is < 1 or > 128 || !ns.Contains('.')) throw new ArgumentException($"'{ns}' isn't a reverse-domain namespace", nameof(ns));
        Entries.Add(new MemidEntry(ns, (ushort)format, payload));
        return this;
    }

    /// <summary>Adds a text-format entry (UTF-8): JSON, TOML, XML or plain text.</summary>
    public MemidCustomMetadata Add(string ns, MetadataFormat format, string text) => Add(ns, format, Encoding.UTF8.GetBytes(text));

    /// <summary>The container's bytes (empty when there are no entries).</summary>
    public byte[] Serialize()
    {
        if (Entries.Count == 0) return [];
        var out_ = new List<byte>(HeaderSize + 64);
        var header = new byte[HeaderSize];
        Magic.CopyTo(header);
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(8), 1);            // version
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(10), HeaderSize);
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(16), (uint)Entries.Count);
        out_.AddRange(header);
        foreach (MemidEntry e in Entries)
        {
            byte[] ns = Encoding.UTF8.GetBytes(e.Namespace);
            var h = new byte[EntryHeaderSize];
            BinaryPrimitives.WriteUInt16LittleEndian(h, (ushort)ns.Length);
            BinaryPrimitives.WriteUInt16LittleEndian(h.AsSpan(2), e.Format);
            BinaryPrimitives.WriteUInt32LittleEndian(h.AsSpan(4), (uint)e.Payload.Length);
            BinaryPrimitives.WriteUInt16LittleEndian(h.AsSpan(8), e.Flags);
            out_.AddRange(h);
            out_.AddRange(ns);
            out_.AddRange(e.Payload);
        }
        byte[] bytes = [.. out_];
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(12), (uint)bytes.Length);  // total size
        return bytes;
    }
}
