{
  description = "Kernel";

  inputs = {

    nixpkgs = {
      type = "github";
      owner = "NixOs";
      repo = "nixpkgs";
      ref = "master";
    };

    flake-utils = {
      type = "github";
      owner = "numtide";
      repo = "flake-utils";
    };

  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        devShells =
          let
            native_build_required = with pkgs; [ gnumake ];
          in
          rec {
            default = kernel;
            kernel = pkgs.mkShell {
              nativeBuildInputs = with pkgs; [
                # building
                grub2
                libisoburn
                nasm
                mtools
                # QOL
                bear
                shellcheck
                clang-tools
                qemu
                doxygen
                graphviz
              ] ++ native_build_required;

              hardeningDisable = [ "fortify" ];

              shellHook = ''
                export ARCH=i686
                export CROSS_COMPILE=toolchain/opt/bin/i686-dailyrun-
              '';
            };

            test = pkgs.mkShell rec {
              nativeBuildInputs = with pkgs; [
                criterion.out
                criterion.dev
                gcovr
              ] ++ native_build_required;

              LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath nativeBuildInputs;
              hardeningDisable = [ "fortify" ];
            };

            # Useful to build our gcc target
            gcc = pkgs.mkShell {
              hardeningDisable = [ "all" ];
              buildInputs = with pkgs; [
                # Required executables
                gnumake
                pkg-config
                autoconf-archive
                autoconf269 # newlib requires using v2.69 exactly
                automake115x
                autogen
                # Required libraries
                gmp.dev
                libmpc
                mpfr.dev
                flex
                bison
                isl
              ];

              shellHook = ''
                export TARGET=i686-dailyrun
                export ARCH=
                export CROSS_COMPILE=
              '';
            };
          };
      }
    );
}
