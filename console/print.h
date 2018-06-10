//This unit allows us to re-direct text messages
// For standard C programs send text messages to the console via "printf"
//     The XCode project shows how you can re-direct these messages to a NSTextView
// For QT programs, we can sent text messages to the cout buffer
//     The QT project shows how you can re-direct these to a Qtextedit
// For R programs, we can intercept these messages.

#ifndef _R_PRINT_H_
	#define _R_PRINT_H_
	#include <stdarg.h>
	#ifdef HAVE_R
		#define R_USE_C99_IN_CXX
		#include <R_ext/Print.h>
		#define printMessage(...) { Rprintf("[dcm2niix info] "); Rprintf(__VA_ARGS__); }
		#define printWarning(...) { Rprintf("[dcm2niix WARNING] "); Rprintf(__VA_ARGS__); }
		#define printError(...) { Rprintf("[dcm2niix ERROR] "); Rprintf(__VA_ARGS__); }
	#else
		#ifdef myUseCOut
			//for piping output to Qtextedit
			// printf and cout buffers are not the same
			// #define printMessage(...) ({fprintf(stdout,__VA_ARGS__);})
			#include <iostream>
			template< typename... Args >
			void printMessage( const char* format, Args... args ) {
			  //std::printf( format, args... );
			  //fprintf(stdout,"Short read on %s: Expected 512, got %zd\n",path, bytes_read);
			  int length = std::snprintf( nullptr, 0, format, args... );
			  if ( length <= 0 ) return;
			  char* buf = new char[length + 1];
			  std::snprintf( buf, length + 1, format, args... );
			  std::cout << buf;
			  delete[] buf;
			}
			#define printError(...) do { printMessage("Error: "); printMessage(__VA_ARGS__);} while(0)
		#else
			#include<stdio.h>
			#define printMessage printf
			//#define printMessageError(...) fprintf (stderr, __VA_ARGS__)
			#ifdef myErrorStdOut //for XCode MRIcro project, pipe errors to stdout not stderr
				#define printError(...) do { printMessage("Error: "); printMessage(__VA_ARGS__);} while(0)
			#else
				#define printError(...) do { fprintf (stderr,"Error: "); fprintf (stderr, __VA_ARGS__);} while(0)
			#endif
		#endif //myUseCOut
		//n.b. use ({}) for multi-line macros http://www.geeksforgeeks.org/multiline-macros-in-c/
		//these next lines work on GCC but not _MSC_VER
		// #define printWarning(...) ({printMessage("Warning: "); printMessage(__VA_ARGS__);})
		// #define printError(...) ({ printMessage("Error: "); printMessage(__VA_ARGS__);})
		#define printWarning(...) do {printMessage("Warning: "); printMessage(__VA_ARGS__);} while(0)

	#endif //HAVE_R
#endif //_R_PRINT_H_
