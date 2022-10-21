{
    "targets": [
        {
            "target_name": "fsuipc",
            "cflags!": [ "-fno-exceptions" ],
            "cflags_cc!": [ "-fno-exceptions" ],
            "msvs_settings": {
                "VCCLCompilerTool": { "ExceptionHandling": 1 },
            },
            "sources": [
                "src/index.cc",
                "src/FSUIPC.cc",
                "src/IPCUser.cc"
            ],
            "include_dirs" : [
                "src",
                "<!(node -p \"require('node-addon-api').include_dir\")"
            ],
        }
    ]
}