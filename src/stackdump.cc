/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/


#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <zorpll/stackdump.h>
#include <zorpll/log.h>

#include <signal.h>
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <fcntl.h>
#include <string.h>

#if HAVE_BACKTRACE
#  include <execinfo.h>
#endif

#ifndef G_OS_WIN32

#if defined(__i386__) && ZORPLIB_ENABLE_STACKDUMP

#ifdef G_OS_WIN32
#define _WIN32_WINNT _WIN32_WINNT_WINXP
#endif


/**
 * Log parts of the current stack in hexadecimal form.
 *
 * @param[in] p signal context
 **/
void
z_stackdump_log_stack(ZSignalContext *p)
{
  unsigned long *esp __attribute__((__may_alias__)) = (unsigned long *) p->esp;
  int i;

  if (!esp)
    {
      /*LOG
        This message indicates that the contents of the pre-signal ESP register is bogus, stackdump will rely on the current stack frame.
       */
      z_log(NULL, CORE_ERROR, 0, "ESP is NULL, stackdump is not available, falling back to current frame;");
      esp = (unsigned long *) &esp;
    }

  for (i = 0; i < 64; i++)
    {
      /*NOLOG*/
      z_log(NULL, CORE_ERROR, 0, "Stack 0x%08lx: %08lx %08lx %08lx %08lx", (unsigned long) esp, esp[0], esp[1], esp[2], esp[3]);
      esp += 4;
    }
}

/**
 * x86 specific stack unrolling function which logs all return addresses and
 * their associated frame pointers.
 *
 * @param[in] p signal context
 **/
void
z_stackdump_log_backtrace(ZSignalContext *p)
{
  /* NOTE: this is i386 specific */
  unsigned long *ebp __attribute__((__may_alias__)) = (unsigned long *) p->ebp;
  
  /*NOLOG*/
  z_log(NULL, CORE_ERROR, 0, "retaddr=0x%lx, ebp=0x%lx", p->eip, (unsigned long) ebp);
  
  while (ebp > (unsigned long *) &ebp && *ebp) 
    {
      /*NOLOG*/
      z_log(NULL, CORE_ERROR, 0, "retaddr=0x%lx, ebp=0x%lx", *(ebp+1), *ebp);
      ebp = (unsigned long *) *ebp;
    }
}

/**
 * Log information found directly in the signal context (register contents).
 *
 * @param[in] p signal context
 **/
void
z_stackdump_log_context(ZSignalContext *p)
{
  /*LOG
    This message indicates that the program caught a fatal signal.
    Please report this event to the Zorp QA team.
   */
  z_log(NULL, CORE_ERROR, 0, 
        "Fatal signal occurred, dumping stack; eax='%08lx', ebx='%08lx', ecx='%08lx', edx='%08lx', esi='%08lx', edi='%08lx', ebp='%08lx', esp='%08lx', eip='%08lx'",
        p->eax, p->ebx, p->ecx, p->edx, p->esi, p->edi, p->ebp, p->esp, p->eip);
}

#else

#define z_stackdump_log_stack(p) 
#define z_stackdump_log_backtrace(p)
#define z_stackdump_log_context(p)

#endif

/**
 * This function reads and logs the contents of the /proc/<pid>/maps file
 * which includes memory mapping for mapped shared objects.
 **/
void
z_stackdump_log_maps(void)
{
  int fd;

  fd = open("/proc/self/maps", O_RDONLY);
  if (fd != -1)
    {
      gchar buf[32768];
      int rc;
      gchar *p, *eol;
      gint avail, end = 0;

      while (1)
        {
          avail = sizeof(buf) - end;
          rc = read(fd, buf + end, avail);

          if (rc == -1)
            break;
	  end += rc;
	  if (rc == 0)
	    break;
          p = buf;
          while (*p && p < (buf + end))
            {
              eol = static_cast<gchar *>(memchr(p, '\n', buf + end - p));
              if (eol)
                {
                  *eol = 0;
                  /*NOLOG*/
                  z_log(NULL, CORE_ERROR, 0, "%s", p);
                  p = eol + 1;
                }
              else
                {
                  end = end - (p - buf);
                  memmove(buf, p, end);
                  break;
                }
            }
        }
      if (end)
	/*NOLOG*/
        z_log(NULL, CORE_ERROR, 0, "%.*s", end, buf);
      close(fd);
    }
  else
    {
      /*LOG
        This message indicates that system was unable to open the /proc/self/maps 
	file to gather information on the previous error and the dump will lack
	this information.
       */
      z_log(NULL, CORE_ERROR, 0, "Error opening /proc/self/maps;");
    }
}

#if HAVE_BACKTRACE
/**
 * This function uses the libc backtrace() and backtrace_symbols() functions
 * to display a backtrace with resolved names. As this does not always
 * succeed (as it uses malloc()) the alternative implementation without
 * symbols is also present.
 **/
void
z_stackdump_log_symbols(void)
{
  void *bt[256];
  int count, i;
  
  count = backtrace(bt, 256);
  if (count)
    {
      gchar **symbols;
     
      /*LOG
        This message reports the number of dumped symbols.
       */
      z_log(NULL, CORE_ERROR, 0, "Symbol dump; count='%d'", count);
      symbols = backtrace_symbols(bt, count);
      for (i = 0; i < count; i++)
        {
          /*NOLOG*/
          z_log(NULL, CORE_ERROR, 0, "%p: %s", bt[i], symbols[i]);
        }
    }
}
#else

#define z_stackdump_log_symbols()

#endif

/**
 * Log stackdump and other information for post-mortem analysis.
 *
 * @param[in] p signal context
 *
 * This function gathers as much information for post-mortem analysis as
 * possible. It is usually called from a fatal signal handler (like SIGSEGV).
 * The current signal context can be queried with the z_stackdump_get_context() macro.
 * This function is Linux & x86 specific.
 **/
void
z_stackdump_log(ZSignalContext * /* p */)
{
  z_stackdump_log_context(p);
  z_stackdump_log_backtrace(p);
  z_stackdump_log_maps();
  z_stackdump_log_stack(p);
  z_stackdump_log_symbols();
}
#else /* G_OS_WIN32 */

#if ZORPLIB_ENABLE_STACKDUMP
#include <windows.h>
#include <tchar.h>
#include <Dbghelp.h>
#include <KnownFolders.h>
#include <ShlObj.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <stdio.h>
#include <Userenv.h>

#include <memory>
#include <string>

/* Visual Studio intelli sense needs this */
#if defined (_M_X64)
#undef _M_IX86
#endif

BOOL z_get_logical_address(VOID *addr, PTSTR module_name, DWORD len, DWORD *section, DWORD *offset);
LPTOP_LEVEL_EXCEPTION_FILTER previous_filter;
static std::string dump_file_name;
static std::string program_version;
static BOOL write_dump_file = FALSE;

/** 
 * This function enables the exception handler code to 
 * write the memory dump into a file.
 **/
void
z_enable_write_dump_file(const gchar *prog_version)
{
  write_dump_file = TRUE;

  program_version = prog_version;
}


typedef BOOL (WINAPI *LPFNMiniDumpWriteDump)(
  HANDLE hProcess,
  DWORD ProcessId,
  HANDLE hFile,
  MINIDUMP_TYPE DumpType,
  PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
  PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
  PMINIDUMP_CALLBACK_INFORMATION CallbackParam
);


std::string
z_format_error_code_to_string(const DWORD &error_code)
{
  LPSTR messageBuffer = nullptr;
  size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, nullptr);
  std::string error_message(messageBuffer, size);
  LocalFree(messageBuffer);
  return error_message;
}

static inline HANDLE
z_stackdump_check_handle(HANDLE h)
{
  if (h == INVALID_HANDLE_VALUE)
    return nullptr;
  return h;
}

/** 
 * This function attempts to write out the memory dump of an exception into a [modulename].dmp file.
 *
 * @param[in] exception_pointers exception pointers
 * 
 * The dump will contain the whole memory of process where the excetpion come off.
 */
void
z_write_minidump(EXCEPTION_POINTERS* exception_pointers)
{
  DWORD error_code = ERROR_SUCCESS;
  std::string error_msg;
  MINIDUMP_EXCEPTION_INFORMATION exp_param;
  LPFNMiniDumpWriteDump lpfnMiniDumpWriteDump = NULL;
  HMODULE hMiniDump;
  exp_param.ThreadId = GetCurrentThreadId();
  exp_param.ExceptionPointers = exception_pointers;
  exp_param.ClientPointers = TRUE;


  hMiniDump = LoadLibrary("dbghelp.dll");
  if (hMiniDump)
    lpfnMiniDumpWriteDump = (LPFNMiniDumpWriteDump) GetProcAddress(hMiniDump, "MiniDumpWriteDump");
  else
  {
    error_code = GetLastError();
    error_msg = z_format_error_code_to_string(error_code);
    z_log(NULL, CORE_ERROR, 0, _T("Failed to load library dbghelp.dll: error code='%08x', error message=%s"), error_code, error_msg);
    return;
  }
  if (!lpfnMiniDumpWriteDump)
    {
      error_code = GetLastError();
      error_msg = z_format_error_code_to_string(error_code);
      z_log(NULL, CORE_ERROR, 0, _T("Failed to get process address of MiniDumpWriteDump: error code='%08x', error message=%s"), error_code, error_msg);
      return;
    }
  std::unique_ptr<void /* HANDLE */, decltype(&CloseHandle)> process(GetCurrentProcess(), CloseHandle);
  if (!process)
    {
      error_code = GetLastError();
      error_msg = z_format_error_code_to_string(error_code);
      z_log(NULL, CORE_ERROR, 0, _T("Failed to open process self: error code='%08x', error message=%s"), error_code, error_msg);
      return;
    }
  std::unique_ptr<void /* HANDLE */, decltype(&CloseHandle)> file(z_stackdump_check_handle(CreateFile(
    dump_file_name.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)),
    CloseHandle);
  if (!file)
    {
      error_code = GetLastError();
      error_msg = z_format_error_code_to_string(error_code);
      z_log(NULL, CORE_ERROR, 0, _T("Failed to create file for minidump: error code='%08x', error message=%s"), error_code, error_msg);
      return;
    }
  if (!lpfnMiniDumpWriteDump(process.get(), GetCurrentProcessId(), file.get(), MiniDumpWithFullMemory, &exp_param, NULL, NULL))
    {
      error_code = GetLastError();
      error_msg = z_format_error_code_to_string(error_code);
      z_log(NULL, CORE_ERROR, 0, _T("Failed to call MiniDumpWrite dump: error code='%08x', error message=%s"), error_code, error_msg);
      return;
    }
  z_log(NULL, CORE_ERROR, 0, _T("Minidump creation succeeded! filename='%s'"), dump_file_name.c_str());
}

/**
 * This function uses the symbols from the pdb file to show the function names on the call stack.
 *
 * @param[in] context current processor context
 * 
 * If there is no symbol in the file, it writes only the addresses into the log.
 **/
void
z_imagehlp_stack_walk(CONTEXT *context)
{
  STACKFRAME64 stack_frame;
  z_log(NULL, CORE_ERROR, 0, _T("Call stack:"));
  z_log(NULL, CORE_ERROR, 0, _T("Address   Frame"));
  /* Could use SymSetOptions here to add the SYMOPT_DEFERRED_LOAds flag */
  memset(&stack_frame, 0, sizeof(stack_frame));
  /*
   * Initialize the STACKFRAME structure for the first call.  This is only
   *  necessary for Intel CPUs, and isn't mentioned in the documentation.
   */
#ifdef _M_IX86
  stack_frame.AddrPC.Offset = context->Eip;
  stack_frame.AddrPC.Mode = AddrModeFlat;
  stack_frame.AddrStack.Offset = context->Esp;
  stack_frame.AddrStack.Mode = AddrModeFlat;
  stack_frame.AddrFrame.Offset = context->Ebp;
  stack_frame.AddrFrame.Mode = AddrModeFlat;
#elif defined(_M_X64)
  stack_frame.AddrPC.Offset = context->Rip;
  stack_frame.AddrPC.Mode = AddrModeFlat;
  stack_frame.AddrStack.Offset = context->Rsp;
  stack_frame.AddrStack.Mode = AddrModeFlat;
  stack_frame.AddrFrame.Offset = context->Rbp;
  stack_frame.AddrFrame.Mode = AddrModeFlat;
#endif
  while (1)
    {
      if (!StackWalk64(
#ifdef _M_IX86
                        IMAGE_FILE_MACHINE_I386,
#elif defined(_M_X64)
                        IMAGE_FILE_MACHINE_AMD64,
#endif
                        GetCurrentProcess(), 
                        GetCurrentThread(),
                        &stack_frame, 
                        context,
                        0, 
                        SymFunctionTableAccess64, 
                        SymGetModuleBase64, 
                        0)
          )
        {
          break;
        }

      if (0 == stack_frame.AddrFrame.Offset) /* Basic sanity check to make sure */
        {
          break;                             /* the frame is OK.  Bail if not. */
        }
      z_log(NULL, CORE_ERROR, 0, _T("%08x  %08x"), stack_frame.AddrPC.Offset, stack_frame.AddrFrame.Offset);

      /*
       * DEBUGHLP is wacky, and requires you to pass in a pointer to a
       * IMAGEHLP_SYMBOL64 structure.  The problem is that this structure is
       * variable length.  That is, you determine how big the structure is
       * at runtime.  This means that you can't use sizeof(struct).
       * So...make a buffer that's big enough, and make a pointer
       * to the buffer.  We also need to initialize not one, but TWO
       * members of the structure before it can be used.
       */
      {
        DWORD64 symbol_displacement = 0; /* Displacement of the input address,
                                            relative to the start of the symbol */
        BYTE symbol_buffer[sizeof(IMAGEHLP_SYMBOL) + 512];
        PIMAGEHLP_SYMBOL64 symbol = (PIMAGEHLP_SYMBOL64)symbol_buffer;
        symbol->SizeOfStruct = sizeof(symbol_buffer);
        symbol->MaxNameLength = 512;

        if (SymGetSymFromAddr64(GetCurrentProcess(), stack_frame.AddrPC.Offset,
                                 &symbol_displacement, symbol))
          {
            z_log(NULL, CORE_ERROR, 0, _T("%hs+%x"), symbol->Name, symbol_displacement);
          }
        else      /* No symbol found.  Print out the logical address instead. */
          {
            TCHAR module_name[MAX_PATH] = _T("");
            DWORD section = 0, offset = 0;
            if (z_get_logical_address((VOID*)stack_frame.AddrPC.Offset, module_name, sizeof(module_name), &section, &offset))
              {
                z_log(NULL, CORE_ERROR, 0, _T("%04x:%08x %s"), section, offset, module_name);
              }
            else
              {
                z_log(NULL, CORE_ERROR, 0, _T(""));
              }

          }
      }//block
  }//while(1)
}

/**
 * Walks the stack, and writes the results to the z_log. 
 *
 * @param[in] context current processor context
 * 
 * Only addresses are supported.
 **/
void
z_intel_stack_walk (PCONTEXT context)
{
#ifdef _M_IX86
  DWORD *frame_pointer, *prev_frame;
  DWORD eip = context->Eip;
  frame_pointer = (DWORD*)context->Ebp;
#elif defined(_M_X64)
  DWORD64 *frame_pointer, *prev_frame;
  DWORD64 eip = context->Rip;
  frame_pointer = (DWORD64*)context->Rbp;
#endif
  z_log(NULL, CORE_ERROR, 0, _T("Call stack:"));
  z_log(NULL, CORE_ERROR, 0, _T("Address   Frame     Logical addr  Module"));
  do
    {
      TCHAR module_name[MAX_PATH] = _T("");
      DWORD section = 0, offset = 0;

      if (!z_get_logical_address((VOID*) eip, module_name, sizeof (module_name), &section, &offset))
        {
          break;
        }
      z_log(NULL, CORE_ERROR, 0, _T("%08x  %08x  %04x:%08x %s"), eip, frame_pointer, section, offset, module_name);
      eip = frame_pointer[1];
      prev_frame = frame_pointer;
#ifdef _M_IX86
      frame_pointer = (DWORD*) frame_pointer[0];  /* precede to next higher frame on stack */
      if ((DWORD) frame_pointer & 3)              /* Frame pointer must be aligned on a DWORD boundary.  Bail if not so.
                                                     (address that is aligned on a 4-BYTE (DWORD) boundary is evenly divisible by 4) 4=100b  3=011b */
        {
          break;                   
        }
      if (frame_pointer <= prev_frame)
        {
          break;
        }
      /* Can two DWORDs be read from the supposed frame address? */
      if (IsBadReadPtr (frame_pointer, sizeof (DWORD) * 2))
        {
          break;
        }
#elif defined(_M_X64)
      frame_pointer = (DWORD64*) pFrame[0];  /* precede to next higher frame on stack */
      if ((DWORD64) frame_pointer & 7)       /* Frame pointer must be aligned on a DWORD64 boundary.  Bail if not so.
                                                (address that is aligned on a 8-BYTE (DWORD64) boundary is evenly divisible by 8) 8=1000b 7=0111b */
        {
          break;
        }
      if (frame_pointer <= prev_frame)
        {
          break;
        }
      /* Can two DWORD64s be read from the supposed frame address? */
      if (IsBadReadPtr (frame_pointer, sizeof (DWORD64) * 2))
        {
          break;
        }
#endif
  }
  while (1);
}

/**
 * This function translates the known exception codes into string format. 
 *
 * @param[in] exception_code exception code
 * 
 * If the exception is unknown, try to resolve the string from the ntdll.dll.
 **/
#define EXCEPTION( x ) case EXCEPTION_##x: return _T(#x);
LPTSTR
z_get_exception_string (DWORD exception_code)
{
  static TCHAR message_buffer[512];

  switch (exception_code)
  {
      EXCEPTION (ACCESS_VIOLATION)
      EXCEPTION (DATATYPE_MISALIGNMENT)
      EXCEPTION (BREAKPOINT)
      EXCEPTION (SINGLE_STEP)
      EXCEPTION (ARRAY_BOUNDS_EXCEEDED)
      EXCEPTION (FLT_DENORMAL_OPERAND)
      EXCEPTION (FLT_DIVIDE_BY_ZERO)
      EXCEPTION (FLT_INEXACT_RESULT)
      EXCEPTION (FLT_INVALID_OPERATION)
      EXCEPTION (FLT_OVERFLOW)
      EXCEPTION (FLT_STACK_CHECK)
      EXCEPTION (FLT_UNDERFLOW)
      EXCEPTION (INT_DIVIDE_BY_ZERO)
      EXCEPTION (INT_OVERFLOW)
      EXCEPTION (PRIV_INSTRUCTION)
      EXCEPTION (IN_PAGE_ERROR)
      EXCEPTION (ILLEGAL_INSTRUCTION)
      EXCEPTION (NONCONTINUABLE_EXCEPTION)
      EXCEPTION (STACK_OVERFLOW)
      EXCEPTION (INVALID_DISPOSITION)
      EXCEPTION (GUARD_PAGE) 
      EXCEPTION (INVALID_HANDLE)
  }

  /* 
   * If not one of the "known" exceptions, try to get the string
   * from NTDLL.DLL's message table.
   */
  FormatMessage (FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_HMODULE,
                 GetModuleHandle (_T("NTDLL.DLL")),
                 exception_code, 0, message_buffer, sizeof (message_buffer), 0);

  return message_buffer;
}

/**
 * This function logs the information about the current process state (registers, callstack).
 *
 * @param[in] exception_info_ptr exception info
 **/
void
z_generate_exception_report(PEXCEPTION_POINTERS exception_info_ptr)
{
  PEXCEPTION_RECORD exception_record;
  TCHAR faulting_module_name[MAX_PATH];
  DWORD section, offset;
  PCONTEXT context;
  DWORD64 symbol_displacement = 0;
  BYTE symbol_buffer[sizeof (IMAGEHLP_SYMBOL64) + 512];
  PIMAGEHLP_SYMBOL64 symbol = (PIMAGEHLP_SYMBOL64) symbol_buffer;
  symbol->SizeOfStruct = sizeof (symbol_buffer);
  symbol->MaxNameLength = 512;

  exception_record = exception_info_ptr->ExceptionRecord;
  /* First print information about the type of exception */
  z_log(NULL, CORE_ERROR, 0, _T("Exception code: %08x %s"),
        exception_record->ExceptionCode,
        z_get_exception_string (exception_record->ExceptionCode));

  /* Now print information about where the fault occured */
  if (!z_get_logical_address (exception_record->ExceptionAddress,
                             faulting_module_name,
                             sizeof (faulting_module_name), &section, &offset))
    {
      z_log(NULL, CORE_ERROR, 0, _T("Fault address:  %08x %02x:%08x "),
            exception_record->ExceptionAddress,
            section, offset);
    }
  else
    {
      z_log(NULL, CORE_ERROR, 0, _T("Fault address:  %08x %02x:%08x %s"),
            exception_record->ExceptionAddress,
            section, offset, faulting_module_name);
    }
  context = exception_info_ptr->ContextRecord;
  if (SymInitialize (GetCurrentProcess(), NULL, TRUE))
    {
      z_imagehlp_stack_walk (context);
    }
  else
    {
      z_intel_stack_walk (context);
    }
  /* winnt.h 2954 _X86_   _CONTEXT // 32 bit Intel cpu and AMD  eax
   * winnt.h 3437 _IA64_  _CONTEXT // 64 bit Intel Itanium cpu  ???
   * winnt.h 2271 _AMD64_ _CONTEXT // 64 bit Intel on AMD cpu   rax 
   * sizeof (DWORD) 4 
   * sizeof (DWORD64) 8
   *
   * Show the registers */
  z_log(NULL, CORE_ERROR, 0, _T("ProcessId: %08x"), GetCurrentProcessId());
#ifdef _M_IX86
  z_log(NULL, CORE_ERROR, 0, _T("Registers:"));

  z_log(NULL, CORE_ERROR, 0, _T("eax:%08x ebx:%08x ecx:%08x edx:%08x esi:%08x edi:%08x"),
        context->Eax, context->Ebx, context->Ecx, context->Edx, context->Esi, context->Edi);
  if (SymGetSymFromAddr64 (GetCurrentProcess (), context->Eip, &symbol_displacement, symbol))
    {
      z_log(NULL, CORE_ERROR, 0, _T("cs:eip:%04x:%08x %S"), context->SegCs, context->Eip, symbol->Name);
    }
  else
    {
      z_log(NULL, CORE_ERROR, 0, _T("cs:eip:%04x:%08x"), context->SegCs, context->Eip);
    }
  z_log(NULL, CORE_ERROR, 0, _T("ss:esp:%04x:%08x  ebp:%08x"),
        context->SegSs, context->Esp, context->Ebp);
  z_log(NULL, CORE_ERROR, 0, _T("ds:%04x es:%04x fs:%04x gs:%04x"),
        context->SegDs, context->SegEs, context->SegFs, context->SegGs);
  z_log(NULL, CORE_ERROR, 0, _T("flags:%08x"), context->EFlags);
#elif defined(_M_X64)
   /* NOTE: add mmx registers and debug registers if needed */
  z_log(NULL, CORE_ERROR, 0, _T("Registers:"));
  z_log(NULL, CORE_ERROR, 0, _T("rax:%016x rbx:%016x rcx:%016x rdx:%016x rsi:%016x rdi:%016x"),
        context->Rax, context->Rbx, context->Rcx, context->Rdx, context->Rsi, context->Rdi);
  z_log(NULL, CORE_ERROR, 0, _T("r08:%016x r09:%016x r10:%016x r11:%016x r12:%016x r13:%016x r14:%016x r15:%016x"),
        context->R8, context->R9, context->R10, context->R11, context->R12, context->R13, context->R14, context->R15);
    
  if (SymGetSymFromAddr64 (GetCurrentProcess (), context->Rip, &symbol_displacement, symbol))
    {
      z_log(NULL, CORE_ERROR, 0, _T("cs:rip:%04x:%016x %S"), context->SegCs, context->Rip, symbol->Name);
    }
  else
    {
      z_log(NULL, CORE_ERROR, 0, _T("cs:rip:%04x:%016x"), context->SegCs, context->Rip);
    }
  z_log(NULL, CORE_ERROR, 0, _T("ss:rsp:%04x:%016x  rbp:%016x"),
        context->SegSs, context->Rsp, context->Rbp);
  z_log(NULL, CORE_ERROR, 0, _T("ds:%04x es:%04x fs:%04x gs:%04x"),
        context->SegDs, context->SegEs, context->SegFs, context->SegGs);
  z_log(NULL, CORE_ERROR, 0, _T("flags:%08x"), context->EFlags);
#endif
  SymCleanup(GetCurrentProcess());
}

/**
 * This function is the unhandled exception filter callback. 
 *
 * @param[in] exception_info_ptr exception info
 * 
 * Win32api will call it. 
 **/
LONG WINAPI
z_unhandled_exception_filter (PEXCEPTION_POINTERS exception_info_ptr)
{
  if (write_dump_file)
  {
    SYSTEMTIME stLocalTime;
    DWORD ret;

    try
      {
        /* Cut off the extension .dmp */
        dump_file_name.resize(dump_file_name.length() - 4);

        std::size_t last_backslash = dump_file_name.find_last_of("\\");
        dump_file_name = dump_file_name.substr(last_backslash + 1);

        PWSTR home_path = nullptr;
        HRESULT hr = SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &home_path);
        if (FAILED(hr))
          throw std::runtime_error("user's home directory not found");

        GetLocalTime( &stLocalTime );
        char file_name[MAX_PATH];

        _snprintf( file_name, MAX_PATH, "%ls\\%s-%s-%04d%02d%02d-%02d%02d%02d-%ld-%ld.dmp",
                        home_path, dump_file_name.c_str(), program_version.c_str(),
                        stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay,
                        stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond,
                        GetCurrentProcessId(), GetCurrentThreadId());

        CoTaskMemFree(home_path);

        dump_file_name = file_name;

        z_write_minidump(exception_info_ptr);
      }
    catch (const std::out_of_range &except)
      {
        z_log(NULL, CORE_ERROR, 0, ("Failed to find dump file path: path='%s'"), dump_file_name);
      }
    catch (const std::runtime_error &except)
      {
        z_log(NULL, CORE_ERROR, 0, ("Failed to write minidump file: error='%s'"), except.what());
      }
  }
  z_generate_exception_report(exception_info_ptr);
  if (previous_filter)
    return previous_filter(exception_info_ptr);
  else
    return EXCEPTION_CONTINUE_SEARCH;
}

/**
 * This funtion sets the windows unhandled exception filter to our z_unhandled_exception_filter.
 **/
#ifndef _SYSCRT
int 
#else
void
#endif
z_set_unhandled_exception_filter(void)
{
  PTCHAR dot = NULL;
  previous_filter = SetUnhandledExceptionFilter(z_unhandled_exception_filter);

  TCHAR file_name[MAX_PATH];
  GetModuleFileName(NULL, file_name, MAX_PATH);
  /* Replace the extension with "dmp" */
  dot = _tcsrchr(file_name, _T('.'));
  if (dot)
    {
      dot++;     /* Advance past the '.' */
      if (_tcslen(dot) >= 3)
        {
          _tcscpy(dot, _T("dmp"));
        }
    }
  dump_file_name = file_name;

#ifndef _SYSCRT
  return 0; 
#else
  return;
#endif
}

/*
 * .CRT$Xpq
 * Where p is the category or group ('I'=C init, 'C'=C++ init, 'P'=Pre-terminators and 'T'=Terminators), 
 * and q is the segment within that group: 'A' is the first and 'Z' is the last (U' for "User" defsects.inc)
 * .CRT$XIU C Initializer Sections
 * .CRT$XCU C++ Constructor Section
 */

#if defined(_MSC_VER)
#  if defined(_M_X64)
#    pragma section(".CRT$XIU", read)
     __declspec(allocate(".CRT$XIU"))
#  else
#    pragma data_seg(".CRT$XIU")
#  endif
#endif
     
static 
#ifndef _SYSCRT
int 
#else
void
#endif
/* crt0data.c:_initp_misc_cfltcvt_tab will call this funtion pointer */
(*pz_set_unhandled_exception_filter)(void) = z_set_unhandled_exception_filter;
#pragma data_seg()

/**
 * This funtion collects information about a memory address form the PE headers.
 *
 * @param[in] addr original address
 * @param[out] module_name module name which contains the address
 * @param[in] len len of the module name
 * @param[out] section file section
 * @param[out] offset offset in the section 
 *
 * @returns TRUE on success
 **/
BOOL
z_get_logical_address (VOID *addr, PTSTR module_name, DWORD len, DWORD *section, DWORD *offset)
{
  MEMORY_BASIC_INFORMATION memory_basic_information;
  DWORD allocation_base;
  PIMAGE_DOS_HEADER pe_dos_header;
  PIMAGE_NT_HEADERS pe_nt_header;
  PIMAGE_SECTION_HEADER pe_section_header;
  DWORD rva;
  unsigned i;

  if (!VirtualQuery (addr, &memory_basic_information, sizeof (memory_basic_information)))
    return FALSE;

  allocation_base = (DWORD) memory_basic_information.AllocationBase;

  if (!GetModuleFileName ((HMODULE) allocation_base, module_name, len))
    return FALSE;

  /* Point to the DOS header in memory */
  pe_dos_header = (PIMAGE_DOS_HEADER) allocation_base;

  /* From the DOS header, find the NT (PE) header */
  pe_nt_header = (PIMAGE_NT_HEADERS) (allocation_base + pe_dos_header->e_lfanew);
  
  /* From the NT header, find the section header */
  pe_section_header = IMAGE_FIRST_SECTION (pe_nt_header);

  /* RVA is offset from module load address */
  rva = (DWORD) addr - allocation_base; 
  
  /* 
   * Iterate through the section table, looking for the one that encompasses
   * the linear address.
   */
  for (i = 0; i < pe_nt_header->FileHeader.NumberOfSections; i++, pe_section_header++)
    {
      DWORD section_start = pe_section_header->VirtualAddress;
      DWORD section_end = section_start + max(pe_section_header->SizeOfRawData, pe_section_header->Misc.VirtualSize);
    
      // Is the address in this section???
      if ((rva >= section_start) && (rva <= section_end))
        {
          /* 
           * Yes, address is in the section.  Calculate section and offset,
           * and store in the "section" & "offset" params, which were
           * passed by reference.
           */
          *section = i + 1;
          *offset = rva - section_start;
          return TRUE;
        }
    }
  return FALSE;
}

#endif /* ZORPLIB_ENABLE_STACKDUMP */
#endif /* ifndef G_OS_WIN32 */
