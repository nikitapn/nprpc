#pragma once

#ifdef _MSC_VER
# ifdef NPRPC_EXPORTS
#	 define NPRPC_API __declspec(dllexport)
# else
#	 define NPRPC_API __declspec(dllimport)
# endif
#else 
# define NPRPC_API
#endif
