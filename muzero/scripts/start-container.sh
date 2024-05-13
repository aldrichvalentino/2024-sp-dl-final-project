#!/bin/bash
set -e
trap 'echo "An error occurred. Exiting..."' ERR

usage() {
    echo "Usage: $0 [OPTION]..."
    echo ""
    echo "Optional arguments:"
    echo "  -h, --help       Give this help list"
    echo "      --image      Select the image name of the container"
    echo "  -v, --volume     Bind mount a volume"
    echo "      --name       Assign a name to the container"
    echo "  -d, --detach     Run container in background and print container ID"
    echo "  --verbose        Enable verbose output"
    exit 1
}

image_name="kds285/minizero:latest"
container_tool=$(basename $(which podman || which docker) 2>/dev/null)
if [[ ! $container_tool ]]; then
    echo "Neither podman nor docker is installed." >&2
    exit 1
fi

container_volume="-v .:/workspace"
container_arguments=""
verbose=0

while :; do
    case $1 in
        -h|--help)
            usage
            ;;
        --image)
            shift
            image_name="$1"
            ;;
        -v|--volume)
            shift
            container_volume="${container_volume} -v $1"
            ;;
        --name)
            shift
            container_arguments="${container_arguments} --name $1"
            ;;
        -d|--detach)
            container_arguments="${container_arguments} -d"
            ;;
        --verbose)
            verbose=1
            ;;
        "")
            break
            ;;
        *)
            echo "Unknown argument: $1"
            usage
            ;;
    esac
    shift
done

container_arguments=$(echo ${container_arguments} | xargs)
run_command="$container_tool run ${container_arguments} --gpus all --cap-add=SYS_PTRACE --security-opt seccomp=unconfined --network=host --ipc=host --rm -it ${container_volume} ${image_name}"
[ $verbose -eq 1 ] && echo $run_command
eval $run_command

