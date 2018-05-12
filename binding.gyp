{
    "targets": [
        {
            "target_name": "fsuipc",
            "sources": [
                "src/index.cc",
                "src/FSUIPC.cc",
                "src/IPCUser.cc"
            ],
            "include_dirs" : [
                "src",
                "<!(node -e \"require('nan')\")"
            ]
        }
    ]
}