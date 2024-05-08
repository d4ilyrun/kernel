{
  description = "A very basic flake";

  inputs = {

    nixpkgs = {
      type = "github";
      owner = "NixOs";
      repo = "nixpkgs";
      ref = "23.05";
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
        pkgs-i686 = pkgs.pkgsCross.i686-embedded;
      in
      rec {
        packages = rec {
          kernel = pkgs-i686.stdenv.mkDerivation {
            name = "kernel";
            src = self;
            version = "0.1.0";
            inherit (devShells.kernel) nativeBuildInputs;

            # FIXME: cannot execute ISO from target script inside nix derivation
            installPhase = ''
              mkdir -p $out
              cp -v kernel/kernel.bin $out
            '';
          };

          default = kernel;
        };

        devShells =
          let
            native_build_required = with pkgs; [ ninja meson ];
          in
          rec {
            default = kernel;
            kernel = pkgs.mkShell.override { inherit (pkgs-i686) stdenv; } {
              nativeBuildInputs = with pkgs; [
                # building
                grub2
                libisoburn
                binutils
                nasm
                # QOL
                bear
                shellcheck
                qemu
              ] ++ native_build_required;

              buildInputs = [ pkgs.pkgsi686Linux.glibc ];
              hardeningDisable = [ "fortify" ];
            };

            test = pkgs.mkShell rec {
              nativeBuildInputs = with pkgs; [
                # Bulding
                gnumake
                ninja
                meson
                nasm
                # Testing
                criterion.out
                criterion.dev
                gcovr
              ] ++ native_build_required;

              LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath nativeBuildInputs;
              hardeningDisable = [ "fortify" ];

              shellHook = ''
                export BUILD_DIR=build
                meson setup --cross-file ./scripts/meson_cross.ini --reconfigure -Dbuildtype=debug "./$BUILD_DIR"
              '';
            };

          };
      }
    );
}
