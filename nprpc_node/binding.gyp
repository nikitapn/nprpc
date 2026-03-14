{
    "targets": [
        {
            "target_name": "nprpc_shm",
            "cflags!": ["-fno-exceptions", "-fno-rtti"],
            "cflags_cc!": ["-fno-exceptions", "-fno-rtti"],
            "cflags_cc": ["-std=c++23", "-fexceptions", "-frtti"],
            "sources": [
                "src/addon.cpp",
                "src/shm_channel_wrapper.cpp",
                "<!@(node -e \"const fs=require('fs'); console.log(fs.existsSync('../src/shm') ? '../src/shm/lock_free_ring_buffer.cpp' : 'nprpc_src/shm/lock_free_ring_buffer.cpp')\")",
                "<!@(node -e \"const fs=require('fs'); console.log(fs.existsSync('../src/flat_buffer.cpp') ? '../src/flat_buffer.cpp' : 'nprpc_src/flat_buffer.cpp')\")",
                "<!@(node -e \"const fs=require('fs'); console.log(fs.existsSync('../src/logging.cpp') ? '../src/logging.cpp' : 'nprpc_src/logging.cpp')\")"
            ],
            "include_dirs": [
                "<!@(node -p \"require('node-addon-api').include\")",
                "<!@(node -e \"const fs=require('fs'); console.log(fs.existsSync('../include') ? '../include' : 'nprpc_include')\")"
            ],
            "libraries": [
                "-lboost_thread",
                "-lssl",
                "-lcrypto",
                "-lrt",
                "-lpthread"
            ],
            "defines": [
                "NAPI_CPP_EXCEPTIONS"
            ],
            "conditions": [
                ["OS=='linux'", {
                    "defines": ["NPRPC_LINUX"]
                }],
                ["OS=='mac'", {
                    "defines": ["NPRPC_MACOS"],
                    "libraries!": ["-lrt"],
                    "xcode_settings": {
                        "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
                        "GCC_ENABLE_CPP_RTTI": "YES",
                        "CLANG_CXX_LANGUAGE_STANDARD": "c++23"
                    }
                }]
            ]
        }
    ]
} # pyright: ignore[reportUnusedExpression]
