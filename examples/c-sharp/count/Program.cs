using System;
using System.IO;
using System.Runtime.InteropServices;

class ZsvInterop {
  private const string ZsvDll =
#if WINDOWS || WIN32 || WIN64 || _WIN32 || __WIN32
    "libzsv.dll";
#elif __MACOS__ || MACOS || OSX
    "libzsv.dylib";
#else
    "libzsv.so"; // Default or throw an error
#endif

  private const string CRT_LIB =
#if WINDOWS
    "msvcrt.dll";
#else
    "libc";
#endif

  [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
  public delegate void ZsvRowHandler(IntPtr parser);

  [DllImport(ZsvDll, CallingConvention = CallingConvention.Cdecl)]
  public static extern IntPtr zsv_new(IntPtr opts);

  [DllImport(ZsvDll, CallingConvention = CallingConvention.Cdecl)]
  public static extern void zsv_delete(IntPtr parser);

  [DllImport(ZsvDll, CallingConvention = CallingConvention.Cdecl)]
  public static extern void zsv_set_row_handler(IntPtr parser, ZsvRowHandler handler);

  [DllImport(ZsvDll, CallingConvention = CallingConvention.Cdecl)]
  public static extern void zsv_set_context(IntPtr parser, IntPtr context);

  [DllImport(ZsvDll, CallingConvention = CallingConvention.Cdecl)]
  public static extern void zsv_set_input(IntPtr parser, IntPtr file);

  [DllImport(ZsvDll, CallingConvention = CallingConvention.Cdecl)]
  public static extern int zsv_parse_more(IntPtr parser);

  [DllImport(ZsvDll, CallingConvention = CallingConvention.Cdecl)]
  public static extern void zsv_finish(IntPtr parser);

  [DllImport(ZsvDll, CallingConvention = CallingConvention.Cdecl)]
  public static extern UIntPtr zsv_cell_count(IntPtr parser);

  [DllImport(ZsvDll, CallingConvention = CallingConvention.Cdecl)]
  public static extern IntPtr zsv_get_cell_str(IntPtr parser, UIntPtr cellIndex);

  [DllImport(ZsvDll, CallingConvention = CallingConvention.Cdecl)]
  public static extern UIntPtr zsv_get_cell_len(IntPtr parser, UIntPtr cellIndex);

  [DllImport(CRT_LIB, CallingConvention = CallingConvention.Cdecl)]
  public static extern IntPtr fopen(string filename, string mode);

  [DllImport(CRT_LIB, CallingConvention = CallingConvention.Cdecl)]
  public static extern int fclose(IntPtr file);
}

class Program {
  static int lineCount = 0;

  static void OnRowParsed(IntPtr parser) {
    lineCount++;

    /** example code for printing the first cell of each row:

    UIntPtr cellCount = ZsvInterop.zsv_cell_count(parser);
    if (cellCount.ToUInt64() > 0) {

      // Index of the first cell
      UIntPtr cellIndex = new UIntPtr(0);

      // Get the pointer to the cell string
      IntPtr cellStrPtr = ZsvInterop.zsv_get_cell_str(parser, cellIndex);

      // Get the length of the cell string
      UIntPtr cellLen = ZsvInterop.zsv_get_cell_len(parser, cellIndex);

      // Convert the cell data to a string
      int length = (int)cellLen.ToUInt64();
      string cellValue = string.Empty;

      if (length > 0) {
        byte[] buffer = new byte[length];
        Marshal.Copy(cellStrPtr, buffer, 0, length);
        cellValue = System.Text.Encoding.UTF8.GetString(buffer);
      }

      Console.WriteLine($"Row {lineCount}: {cellValue}");
    }
    **/
  }

  static void Main(string[] args) {
    if (args.Length != 1) {
      Console.WriteLine("Usage: count-cs <csv_file_path>");
      return;
    }

    string csvFilePath = args[0];

    if (!File.Exists(csvFilePath)) {
      Console.WriteLine($"Error: File '{csvFilePath}' does not exist.");
      return;
    }

    IntPtr parser = ZsvInterop.zsv_new(IntPtr.Zero);
    if (parser == IntPtr.Zero) {
      Console.WriteLine("Failed to create zsv parser.");
      return;
    }

    ZsvInterop.ZsvRowHandler handler = new ZsvInterop.ZsvRowHandler(OnRowParsed);
    ZsvInterop.zsv_set_row_handler(parser, handler);
    ZsvInterop.zsv_set_context(parser, parser);

    // Open the file using fopen
    IntPtr file = ZsvInterop.fopen(csvFilePath, "r");
    if (file == IntPtr.Zero) {
      Console.WriteLine($"Error: Failed to open file '{csvFilePath}'.");
      ZsvInterop.zsv_delete(parser);
      return;
    }

    ZsvInterop.zsv_set_input(parser, file);

    int result = 0;
    while (result == 0) {
      result = ZsvInterop.zsv_parse_more(parser);
    }

    ZsvInterop.zsv_finish(parser);
    ZsvInterop.zsv_delete(parser);
    ZsvInterop.fclose(file);

    if (result != 2) // zsv_status_no_more_input
    {
      Console.WriteLine("Parsing failed: " + $"{result}");
    } else {
      Console.WriteLine($"{lineCount-1}");
    }
  }
}
