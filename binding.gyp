{
    "targets": [
        {
            "target_name": "fsuipc",
            "sources": [
                "src/index.cc",
                "src/fsuipc.cc",
                "include/fsuipc/IPCuser64.c",
                "include/fsuipc/vector.c"
            ],
            "include_dirs" : [
                "src",
                "<!(node -e \"require('nan')\")"
            ]
        }
    ]
}