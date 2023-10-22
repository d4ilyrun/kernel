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
      {
        devShells = rec {
          default = kernel;
          kernel = pkgs.mkShell.override { inherit (pkgs-i686) stdenv; } {
            nativeBuildInputs = with pkgs; [
              gnumake
              grub2
              bear
              libisoburn
              shellcheck
              binutils
            ];
            buildInputs = with pkgs.pkgsi686Linux; [
              glibc
            ];
          };
        };
      }
    );
}
