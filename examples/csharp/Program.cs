// Reading a binary's MEMID identity from C# through the C ABI (memid/c_api.h). Usage: MemidRead <memid.dll> <binary>
using System;
using System.Reflection;
using System.Runtime.InteropServices;

internal static class Program
{
    // memid_descriptor_info, field for field (sequential layout, natural alignment like the C struct)
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    private unsafe struct DescriptorInfo
    {
        public uint StructSize;
        public ushort FormatMajor, FormatMinor;
        public uint Flags;
        public fixed byte ModId[16];
        public fixed byte PublisherId[16];
        public fixed byte BuildId[16];
        public ushort VersionMajor, VersionMinor, VersionPatch;
        public ushort ReleaseChannel, Architecture, OperatingSystem;
        public byte BinaryFormat, Language;
        public ushort Runtime;
        public ulong BuildTimestamp, ToolchainId;
        public uint GameId, GameVersionMin, GameVersionMax;
        public uint InteropMin, InteropMax;
        public ulong Capabilities, ProtocolFlags;
        public uint ExtensionCount;
        public fixed byte ReverseDomain[97];
        public fixed byte Name[65];
        public fixed byte Publisher[49];
        public fixed byte Homepage[65];
    }

    [DllImport("memid", CallingConvention = CallingConvention.Cdecl)]
    private static extern unsafe int memid_read_file([MarshalAs(UnmanagedType.LPUTF8Str)] string path, DescriptorInfo* info);

    [DllImport("memid", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr memid_status_text(int status);

    [DllImport("memid", CallingConvention = CallingConvention.Cdecl)]
    private static extern uint memid_abi_version();

    private static unsafe string Text(byte* field, int size)
    {
        int length = 0;
        while (length < size && field[length] != 0) ++length;
        return System.Text.Encoding.UTF8.GetString(field, length);
    }

    private static unsafe int Main(string[] args)
    {
        if (args.Length != 2)
        {
            Console.Error.WriteLine("usage: MemidRead <memid library> <binary>");
            return 1;
        }
        string library = args[0];
        NativeLibrary.SetDllImportResolver(Assembly.GetExecutingAssembly(),
            (name, _, _) => name == "memid" ? NativeLibrary.Load(library) : IntPtr.Zero);

        DescriptorInfo info = default;
        info.StructSize = (uint)sizeof(DescriptorInfo);
        int status = memid_read_file(args[1], &info);
        if (status != 0 && status != 12)
        {
            Console.WriteLine($"{args[1]}: no identity ({Marshal.PtrToStringAnsi(memid_status_text(status))})");
            return 3;
        }
        var modId = new byte[16];
        for (int i = 0; i < 16; ++i) modId[i] = info.ModId[i];
        // MEMID stores UUIDs in text order: Guid's big-endian constructor reads them that way
        var guid = new Guid(modId, bigEndian: true);
        Console.WriteLine($"{Text(info.Name, 65)} {info.VersionMajor}.{info.VersionMinor}.{info.VersionPatch} " +
                          $"({Text(info.ReverseDomain, 97)}) {guid} [C ABI {memid_abi_version()}]");
        return 0;
    }
}
