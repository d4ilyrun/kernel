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

        devShells = rec {
          default = kernel;
          kernel = pkgs.mkShell.override { inherit (pkgs-i686) stdenv; } {
            nativeBuildInputs = with pkgs; [
              gnumake
              meson
              ninja
              grub2
              bear
              libisoburn
              shellcheck
              binutils
              qemu
              nasm
            ];
            buildInputs = with pkgs.pkgsi686Linux; [
              glibc
            ];
          };
        };
      }
    );
}
