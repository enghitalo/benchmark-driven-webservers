# To install NASM from source using the provided URL, follow these steps:

1. **Download the source tarball:**

   ```bash
   wget https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/nasm-2.16.03.tar.gz
   ```

2. **Extract the tarball:**

   ```bash
   tar -xf nasm-2.16.03.tar.gz
   ```

3. **Navigate to the extracted directory:**

   ```bash
   cd nasm-2.16.03
   ```

4. **Configure the build environment:**

   ```bash
   ./configure
   ```

   - This step prepares the source code for compilation and checks for dependencies.

5. **Compile the source code:**

   ```bash
   make
   ```

   - This builds the NASM binaries.

6. **Install NASM:**

   ```bash
   sudo make install
   ```

   - This installs NASM into the system (usually under `/usr/local/bin`).

7. **Verify the installation:**
   ```bash
   nasm -v
   ```
   - You should see the version `2.16.03` displayed, confirming the installation.

# To run C file

_C_

```sh
clear && gcc -o 11_non_blocking_mode.out 11_non_blocking_mode.c -lpthread && valgrind ./11_non_blocking_mode.out
```

# debug

```sh
valgrind ./11_non_blocking_mode
```

**Test with `curl`**:
Open another terminal and run:

```sh
curl http://localhost:8080
```

# generate Assembly from C code

```sh
gcc -S 11_non_blocking_mode.c
```

# WRK

```
wrk -t16 -c512 -d10s http://127.0.0.1:8080
```
