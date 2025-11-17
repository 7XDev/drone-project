# Software Requirements

The following software tools are required or recommended for working with the drone project.

## Necessary Software

### 3D Printer Slicer
**Purpose:** Export G-code files to print the necessary parts.
  - [Creality Print](https://www.creality.com/pages/download)
  - [Bambu Studio](https://www.bambulab.com/en/software/studio)
  - [Cura](https://ultimaker.com/software/ultimaker-cura)

**Linux Installation:**
1. Download the AppImage or DEB package from the respective website.
2. For AppImage:
```
chmod +x <filename>
./filename.AppImage
```

3. For DEB:
```
sudo dpkg -i <filename>.deb
sudo apt-get install -f
```

## Optional Software

### Code Editor
**Purpose:** Modify the source code efficiently.

- **Recommended:** [Visual Studio Code](https://code.visualstudio.com/)

**Linux Installation:**
1. Download the DEB or RPM package from the [official website](https://code.visualstudio.com/).
2. For DEB:
```
sudo dpkg -i code_<version>.deb
sudo apt-get install -f
```
3. For RPM:
```
sudo rpm -i code-<version>.rpm
```
4. Alternatively, install via Snap:
```
sudo snap install --classic code
```

## Development Tools

### Using C

#### GCC Compiler
**Purpose:** Compile and modify the source code.

- **Download Link:** [GCC](https://gcc.gnu.org/)

**Linux Installation:**
```
sudo apt update
sudo apt install build-essential
```

#### ESP-IDF
**Purpose:** Flash modified software onto the drone.

- **Download Link:** [ESP-IDF](https://github.com/espressif/esp-idf)

**Linux Installation:**
1. Clone the repository:
```
git clone --recursive https://github.com/espressif/esp-idf.git
```
2. Navigate to the directory:
```
cd esp-idf
```
3. Run the installation script:
```
./install.sh
```
4. Set up the environment:
```
. ./export.sh
```

### Using C++

#### Arduino IDE
**Purpose:** Code editor and flasher in a single solution.

- **Download Link:** [Arduino IDE](https://www.arduino.cc/en/software)

**Linux Installation:**
1. Download the tarball from the [official website](https://www.arduino.cc/en/software).
2. Extract the tarball:
```
tar -xvf arduino-ide-<version>-linux64.tar.xz
```
3. Navigate to the extracted folder:
```
cd arduino-ide-<version>
```
4. Run the installer:
```
sudo ./install.sh
```