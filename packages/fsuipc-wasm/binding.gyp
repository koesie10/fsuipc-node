{
    "targets": [
        {
            "target_name": "fsuipcwasm",
            "cflags!": [ "-fno-exceptions" ],
            "cflags_cc!": [ "-fno-exceptions" ],
            "msvs_settings": {
                "VCCLCompilerTool": {
                    "AdditionalOptions": [
                        "-std:c++17"
                    ],
                    "ExceptionHandling": 1,
                },
            },
            "sources": [
                "src/index.cc",
                "src/FSUIPCWASM.cc",
                "third_party/FSUIPC_WAPI/CDAIdBank.cpp",
                "third_party/FSUIPC_WAPI/ClientDataArea.cpp",
                "third_party/FSUIPC_WAPI/Logger.cpp",
                "third_party/FSUIPC_WAPI/WASMIF.cpp"
            ],
            "include_dirs" : [
                "src",
                "third_party",
                "<!(node -p \"require('node-addon-api').include_dir\")",
                "<(msfs_sdk)/SimConnect SDK/include"
            ],
            "link_settings": {
                "libraries": [
                    "-lshlwapi",
                    "-luser32",
                    "-lws2_32",
                    "-lshell32",
                    "-lole32",
                ],
                "library_dirs": [
                    "<(msfs_sdk)/SimConnect SDK/lib/static",
                ],
            },
            # https://github.com/nodejs/node-gyp/issues/1686#issuecomment-483665821
            "configurations": {
                "Debug": {
                    "msvs_settings": {
                        "VCCLCompilerTool": {
                            # 0 - MultiThreaded (/MT)
                            # 1 - MultiThreadedDebug (/MTd)
                            # 2 - MultiThreadedDLL (/MD)
                            # 3 - MultiThreadedDebugDLL (/MDd)
                            "RuntimeLibrary": 3,
                        },
                        "VCLinkerTool": {
                            "AdditionalDependencies": [
                                "SimConnect_debug.lib"
                            ]
                        },
                    }
                },
                "Release": {
                    "msvs_settings": {
                        "VCCLCompilerTool": {
                            "RuntimeLibrary": 2,  # MultiThreadedDLL
                        },
                        "VCLinkerTool": {
                            "AdditionalDependencies": [
                                "SimConnect.lib"
                            ]
                        },
                    },
                },
            },
        }
    ],
    "variables": {
        "msfs_sdk": "<!(node -p \"process.env.MSFS_SDK || \\\"C:\\MSFS SDK\\\"\")"
    }
}