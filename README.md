The .pod execution syntax
name: name_of_pod
cpu: n(# of cpus)
memory: nMB

main: <the main process>

sidecars:
    -logging <the logging process>

about the logging process
the stdin of the logging process will be the stdout of the main process(the stdout of the main will be copied to the logging process), stdout of logging process will be the default

you will also need to upload a zip file to the web interface with the same name as the pod(name_of_pod) that contains that main and sidecar programs, they shouldnt be inside any other directories 