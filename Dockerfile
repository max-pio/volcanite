FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# ENV NVIDIA_DRIVER_CAPABILITIES compute,graphics,utility

# Install Vulkan
RUN apt update \
    && apt install -y \
    libxext6 \
    libvulkan1 \
    libvulkan-dev \
    vulkan-tools

# Install Other Dependencies
RUN apt install -y build-essential cmake xorg-dev

# NOTE: this dockerfile allows to build Volcanite and to compress segmentation volumes into .csgv files.
#       You will not be able to use Volcanite's rendering features as the software llvmpipe will be the only available Vulkan device!

