{
  inputs = {
    flake-parts.url = "github:hercules-ci/flake-parts";
    nixpkgs.url = "github:nixos/nixpkgs";
    rust-overlay = {
      url = "github:oxalica/rust-overlay";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };
  outputs =
    inputs@{ flake-parts, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [ "x86_64-linux" "aarch64-darwin" "x86_64-darwin" "aarch64-linux" ];
      perSystem =
        { system, ... }:
        let
          pkgs = import inputs.nixpkgs {
            inherit system;
            config.allowUnfree = true;
            overlays = [ inputs.rust-overlay.overlays.default ];
          };
          rustToolchain = pkgs.rust-bin.nightly.latest.default.override {
            extensions = [ "rust-src" "rust-analyzer" "llvm-tools" ];
          };
          tools = with pkgs; [
            socat
            open-watcom-bin
            minicom
            dosbox-x
            nasm
            qemu
            mtools
            rustToolchain
            cargo-binutils
          ];
        in
        {
          devShells.default = pkgs.mkShell {
            nativeBuildInputs = tools;
            WATCOM = "${pkgs.open-watcom-bin}";
          };
          packages.default = pkgs.buildEnv {
            name = "pico-386-tools";
            paths = tools;
          };
        };
    };
}
