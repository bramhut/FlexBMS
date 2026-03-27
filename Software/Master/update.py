import os
import shutil
from sys import platform

# For Linux compatibility
# if platform == "linux":
#     shutil.copy("stm32pio-linux.ini", "stm32pio.ini")
# elif platform == "darwin":
#     shutil.copy("stm32pio-macos.ini", "stm32pio.ini")
# else:
#     shutil.copy("stm32pio-windows.ini", "stm32pio.ini")

# CubeMX is the reason all these file paths begin with a capital letter
# Trust me I hate it too

# Move all existing files in /Src and /Inc directories to their respective /Temp directories
if not os.path.exists("./Src/Temp"):
    os.makedirs("./Src/Temp")
for filename in os.listdir("./Src"):
    shutil.move(os.path.join("./Src", filename), "./Src/Temp")

if not os.path.exists("./Inc/Temp"):
    os.makedirs("./Inc/Temp")
for filename in os.listdir("./Inc"):
    shutil.move(os.path.join("./Inc", filename), "./Inc/Temp")

# Move all files from /Src/Temp/Generated to /Src and all files from /Inc/Temp/Generated to /Inc
if os.path.exists("./Src/Temp/Generated"):
    for filename in os.listdir("./Src/Temp/Generated"):
        shutil.move(os.path.join("./Src/Temp/Generated", filename), "./Src")
if os.path.exists("./Inc/Temp/Generated"):
    for filename in os.listdir("./Inc/Temp/Generated"):
        shutil.move(os.path.join("./Inc/Temp/Generated", filename), "./Inc")

# Rename all *.cpp files in /Src subdirectory to *.c
for filename in os.listdir("./Src"):
    if filename.endswith('.cpp') and os.path.isfile(os.path.join("./Src", filename)):
        os.rename(os.path.join("./Src", filename), os.path.join("./Src", filename[:-4] + '.c'))

# Run the command 'stm32pio generate' on both /Src and /Inc directories
os.system("stm32pio generate")

# Rename all *.c files in /Src subdirectory back to *.cpp. Exclude usbd_conf.c
for filename in os.listdir("./Src"):
    if filename.endswith('.c') and os.path.isfile(os.path.join("./Src", filename)) and filename != "usbd_conf.c":
        os.rename(os.path.join("./Src", filename), os.path.join("./Src", filename[:-2] + '.cpp'))

# Move all files from /Src to /Src/Temp/Generated and all files from /Inc to /Inc/Temp/Generated
if not os.path.exists("./Src/Temp/Generated"):
    os.makedirs("./Src/Temp/Generated")
for filename in os.listdir("./Src"):
    if os.path.isfile(os.path.join("./Src", filename)):
        shutil.move(os.path.join("./Src", filename), "./Src/Temp/Generated")
if not os.path.exists("./Inc/Temp/Generated"):
    os.makedirs("./Inc/Temp/Generated")
for filename in os.listdir("./Inc"):
    if os.path.isfile(os.path.join("./Inc", filename)):
        shutil.move(os.path.join("./Inc", filename), "./Inc/Temp/Generated")

# Move all files in /Src/Temp and /Inc/Temp back to /Src and /Inc, respectively
for filename in os.listdir("./Src/Temp"):
    shutil.move(os.path.join("./Src/Temp", filename), "./Src")
for filename in os.listdir("./Inc/Temp"):
    shutil.move(os.path.join("./Inc/Temp", filename), "./Inc")

# Remove the /Src/Temp and /Inc/Temp directories
if os.path.exists("./Src/Temp"):
    os.rmdir("./Src/Temp")
if os.path.exists("./Inc/Temp"):
    os.rmdir("./Inc/Temp")
