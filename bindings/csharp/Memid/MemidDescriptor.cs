using System.Buffers.Binary;
using System.Text;

namespace Memid;

/// <summary>Release channel of a build (descriptor offset 58).</summary>
public enum ReleaseChannel : ushort { Unspecified = 0, Stable = 1, Beta = 2, Alpha = 3, Development = 4, Nightly = 5 }

/// <summary>CPU architecture (offset 60).</summary>
public enum Architecture : ushort { Unknown = 0, X86 = 1, X86_64 = 2, Arm = 3, Arm64 = 4, Any = 0xFFFF }

/// <summary>Operating system (offset 62).</summary>
public enum OperatingSystemId : ushort { Unknown = 0, Windows = 1, Linux = 2, MacOS = 3, Any = 0xFFFF }

/// <summary>Binary format (offset 64).</summary>
public enum BinaryFormat : byte { Unknown = 0, Pe = 1, Elf = 2, MachO = 3 }

/// <summary>Source language (offset 65).</summary>
public enum Language : byte { Unknown = 0, C = 1, Cpp = 2, CSharp = 3, Rust = 4, Zig = 5, D = 6, Go = 7, Pascal = 8, Other = 255 }

/// <summary>Runtime the binary needs (offset 66).</summary>
public enum Runtime : ushort { None = 0, Mirage = 1, DotNet = 2, Lua = 3, Other = 0xFFFF }

/// <summary>Declared functionality (offset 128).</summary>
[Flags]
public enum Capabilities : ulong
{
    None = 0, Gameplay = 1 << 0, Rendering = 1 << 1, UI = 1 << 2, Audio = 1 << 3, Input = 1 << 4, Networking = 1 << 5,
    Filesystem = 1 << 6, Scripting = 1 << 7, Content = 1 << 8, NativeHooks = 1 << 9, Tooling = 1 << 10,
}

/// <summary>
/// A MEMID 1.1 descriptor: the 428-byte identity of a binary (docs/MEMID_FORMAT.md). <see cref="Serialize"/> writes it
/// as the C++ library's <c>memid::serialize</c> does; put the result in a <c>.memid</c> section or a <c>MEMID</c>
/// RT_RCDATA resource. Extensions are not written (none are defined yet).
/// </summary>
public sealed class MemidDescriptor
{
    /// <summary>Size of a 1.1 descriptor.</summary>
    public const int Size = 428;

    /// <summary>The descriptor's magic: "MEMID" CR LF SUB.</summary>
    public static ReadOnlySpan<byte> Magic => "MEMID\r\n\x1A"u8;

    /// <summary>The mod's permanent UUID (required, not empty).</summary>
    public Guid ModId { get; set; }
    /// <summary>The publisher's UUID (<see cref="Guid.Empty"/>: none).</summary>
    public Guid PublisherId { get; set; }
    /// <summary>Version (major, minor, patch; each 0..65535).</summary>
    public Version Version { get; set; } = new(0, 0, 0);
    /// <summary>Release channel.</summary>
    public ReleaseChannel Channel { get; set; }
    /// <summary>Architecture.</summary>
    public Architecture Architecture { get; set; }
    /// <summary>Operating system.</summary>
    public OperatingSystemId OperatingSystem { get; set; } = OperatingSystemId.Windows;
    /// <summary>Binary format.</summary>
    public BinaryFormat BinaryFormat { get; set; } = BinaryFormat.Pe;
    /// <summary>Source language.</summary>
    public Language Language { get; set; } = Language.CSharp;
    /// <summary>Runtime.</summary>
    public Runtime Runtime { get; set; }
    /// <summary>One build of this version (<see cref="Guid.Empty"/>: none).</summary>
    public Guid BuildId { get; set; }
    /// <summary>Build time (null: unknown).</summary>
    public DateTimeOffset? BuildTime { get; set; }
    /// <summary>Compiler family and version, packed (16 bits each: family, major, minor, patch; 6 = .NET).</summary>
    public ulong ToolchainId { get; set; }
    /// <summary>Game (1: Mirror's Edge 2008 PC; 0: none).</summary>
    public uint GameId { get; set; }
    /// <summary>Lowest game version, packed major &lt;&lt; 24 | minor &lt;&lt; 16 | patch (0: no limit).</summary>
    public uint GameVersionMin { get; set; }
    /// <summary>Highest game version, packed (0: no limit).</summary>
    public uint GameVersionMax { get; set; }
    /// <summary>Lowest protocol version spoken (0: no limit).</summary>
    public uint InteropMin { get; set; }
    /// <summary>Highest protocol version spoken (0: no limit).</summary>
    public uint InteropMax { get; set; }
    /// <summary>Declared functionality.</summary>
    public Capabilities Capabilities { get; set; }
    /// <summary>Protocol flags (bit 0: exports memid_identity_descriptor; bit 1: speaks MIRAGE's mod ABI).</summary>
    public ulong ProtocolFlags { get; set; }
    /// <summary>Flags (bit 0: debug build; bit 1: has custom metadata).</summary>
    public uint Flags { get; set; }
    /// <summary>Reverse-domain id, e.g. <c>com.example.my-mod</c> (at most 96 bytes of UTF-8).</summary>
    public string ReverseDomain { get; set; } = "";
    /// <summary>Display name (at most 64 bytes of UTF-8).</summary>
    public string Name { get; set; } = "";
    /// <summary>Publisher display name (at most 48 bytes of UTF-8).</summary>
    public string Publisher { get; set; } = "";
    /// <summary>Homepage URL (at most 64 bytes of UTF-8).</summary>
    public string Homepage { get; set; } = "";

    /// <summary>The 428 bytes. Text longer than its field is cut at a UTF-8 character boundary.</summary>
    /// <exception cref="InvalidOperationException">No <see cref="ModId"/>, or a version part over 65535.</exception>
    public byte[] Serialize()
    {
        if (ModId == Guid.Empty) throw new InvalidOperationException("a MEMID descriptor needs a mod id");
        var d = new byte[Size];
        var s = d.AsSpan();
        Magic.CopyTo(s);
        BinaryPrimitives.WriteUInt16LittleEndian(s[8..], 1);                // format 1.1
        BinaryPrimitives.WriteUInt16LittleEndian(s[10..], 1);
        BinaryPrimitives.WriteUInt32LittleEndian(s[12..], Size);
        BinaryPrimitives.WriteUInt32LittleEndian(s[16..], Flags);
        ModId.TryWriteBytes(s[20..], bigEndian: true, out _);               // UUIDs in text order
        PublisherId.TryWriteBytes(s[36..], bigEndian: true, out _);
        BinaryPrimitives.WriteUInt16LittleEndian(s[52..], Part(Version.Major));
        BinaryPrimitives.WriteUInt16LittleEndian(s[54..], Part(Version.Minor));
        BinaryPrimitives.WriteUInt16LittleEndian(s[56..], Part(Math.Max(Version.Build, 0)));
        BinaryPrimitives.WriteUInt16LittleEndian(s[58..], (ushort)Channel);
        BinaryPrimitives.WriteUInt16LittleEndian(s[60..], (ushort)Architecture);
        BinaryPrimitives.WriteUInt16LittleEndian(s[62..], (ushort)OperatingSystem);
        d[64] = (byte)BinaryFormat;
        d[65] = (byte)Language;
        BinaryPrimitives.WriteUInt16LittleEndian(s[66..], (ushort)Runtime);
        BuildId.TryWriteBytes(s[68..], bigEndian: true, out _);
        BinaryPrimitives.WriteUInt64LittleEndian(s[84..], BuildTime is { } t ? (ulong)Math.Max(0, t.ToUnixTimeSeconds()) : 0);
        BinaryPrimitives.WriteUInt64LittleEndian(s[92..], ToolchainId);
        BinaryPrimitives.WriteUInt32LittleEndian(s[100..], GameId);
        BinaryPrimitives.WriteUInt32LittleEndian(s[104..], GameVersionMin);
        BinaryPrimitives.WriteUInt32LittleEndian(s[108..], GameVersionMax);
        BinaryPrimitives.WriteUInt32LittleEndian(s[112..], InteropMin);
        BinaryPrimitives.WriteUInt32LittleEndian(s[116..], InteropMax);
        BinaryPrimitives.WriteUInt64LittleEndian(s[128..], (ulong)Capabilities);
        BinaryPrimitives.WriteUInt64LittleEndian(s[136..], ProtocolFlags);
        Text(s.Slice(144, 96), ReverseDomain);
        Text(s.Slice(240, 64), Name);
        Text(s.Slice(304, 48), Publisher);
        Text(s.Slice(352, 64), Homepage);
        return d;                                                           // No extensions: offset, size, flags 0
    }

    /// <summary>Packs a game version the way the descriptor stores it.</summary>
    public static uint PackGameVersion(int major, int minor, int patch) =>
        ((uint)major << 24) | ((uint)(minor & 0xFF) << 16) | (uint)(patch & 0xFFFF);

    static ushort Part(int v) =>
        v is >= 0 and <= ushort.MaxValue ? (ushort)v : throw new InvalidOperationException($"version part {v} is outside 0..65535");

    // UTF-8, NUL padded, cut at a character boundary like the C++ CutUtf8
    static void Text(Span<byte> field, string value)
    {
        byte[] utf8 = Encoding.UTF8.GetBytes(value);
        int n = Math.Min(utf8.Length, field.Length);
        if (n < utf8.Length)
            while (n > 0 && (utf8[n] & 0xC0) == 0x80) n--;
        utf8.AsSpan(0, n).CopyTo(field);
    }
}
