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
                "../src/shm/lock_free_ring_buffer.cpp",
                "../src/flat_buffer.cpp"
            ],
            "include_dirs": [
                "<!@(node -p \"require('node-addon-api').include\")",
                "../include"
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
}
