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
   <pre class="markdown-code-block">chmod +x &lt;filename&gt;.AppImage
./&lt;filename&gt;.AppImage</pre>
3. For DEB:
   <pre class="markdown-code-block">sudo dpkg -i &lt;filename&gt;.deb
sudo apt-get install -f</pre>

## Optional Software

### Code Editor
**Purpose:** Modify the source code efficiently.

- **Recommended:** [Visual Studio Code](https://code.visualstudio.com/)

**Linux Installation:**
1. Download the DEB or RPM package from the [official website](https://code.visualstudio.com/).
2. For DEB:
   <pre class="markdown-code-block">sudo dpkg -i code_&lt;version&gt;.deb
sudo apt-get install -f</pre>
3. For RPM:
   <pre class="markdown-code-block">sudo rpm -i code-&lt;version&gt;.rpm</pre>
4. Alternatively, install via Snap:
   <pre class="markdown-code-block">sudo snap install --classic code</pre>

## Development Tools

### Using C

#### GCC Compiler
**Purpose:** Compile and modify the source code.

- **Download Link:** [GCC](https://gcc.gnu.org/)

**Linux Installation:**
<pre class="markdown-code-block">sudo apt update
sudo apt install build-essential</pre>

#### ESP-IDF
**Purpose:** Flash modified software onto the drone.

- **Download Link:** [ESP-IDF](https://github.com/espressif/esp-idf)

**Linux Installation:**
1. Clone the repository:
   <pre class="markdown-code-block">git clone --recursive https://github.com/espressif/esp-idf.git</pre>
2. Navigate to the directory:
   <pre class="markdown-code-block">cd esp-idf</pre>
3. Run the installation script:
   <pre class="markdown-code-block">./install.sh</pre>
4. Set up the environment:
   <pre class="markdown-code-block">. ./export.sh</pre>

### Using C++

#### Arduino IDE
**Purpose:** Code editor and flasher in a single solution.

- **Download Link:** [Arduino IDE](https://www.arduino.cc/en/software)

**Linux Installation:**
1. Download the tarball from the [official website](https://www.arduino.cc/en/software).
2. Extract the tarball:
   <pre class="markdown-code-block">tar -xvf arduino-ide-&lt;version&gt;-linux64.tar.xz</pre>
3. Navigate to the extracted folder:
   <pre class="markdown-code-block">cd arduino-ide-&lt;version&gt;</pre>
4. Run the installer:
   <pre class="markdown-code-block">sudo ./install.sh</pre>