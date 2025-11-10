// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

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
