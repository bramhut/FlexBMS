# This file is used to enable the FPU and add the necessary flags to the build environment.
Import("env")
flags = [
   "-mfloat-abi=hard",
   "-mfpu=fpv4-sp-d16"
]
env.Append(CCFLAGS=flags, LINKFLAGS=flags)