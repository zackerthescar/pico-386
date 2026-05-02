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
            gnumake
            python3
            binutils
          ];
          zlib1211 = pkgs.fetchurl {
            url = "https://zlib.net/fossils/zlib-1.2.11.tar.gz";
            hash = "sha256-w+Xp/dUATctUL+2l7k8P8HRGKLr47S3V1m+MoRl8saE=";
          };
          projectCargoDeps = pkgs.rustPlatform.importCargoLock {
            lockFile = ./rust/Cargo.lock;
          };
          rustStdCargoDeps = pkgs.rustPlatform.importCargoLock {
            lockFile = ./nix/rust-std-Cargo.lock;
          };
          cargoDeps = pkgs.runCommand "pico-386-cargo-vendor-dir" { } ''
            mkdir -p $out
            cp -R ${rustStdCargoDeps}/* $out/
            cp -R ${projectCargoDeps}/* $out/
            rm -f $out/Cargo.lock
            cp ${./rust/Cargo.lock} $out/Cargo.lock
          '';
        in
        {
          packages = {
            default = pkgs.stdenv.mkDerivation {
              pname = "pico-386";
              version = "0.1.0";

              src = ./.;

              nativeBuildInputs = tools ++ [ pkgs.rustPlatform.cargoSetupHook ];

              cargoRoot = "rust";
              inherit cargoDeps;

              WATCOM = "${pkgs.open-watcom-bin}";

              postPatch = ''
                mkdir -p zlib
                tar xzf ${zlib1211} --strip-components=1 -C zlib
              '';

              buildPhase = ''
                runHook preBuild
                make pico
                runHook postBuild
              '';

              installPhase = ''
                runHook preInstall
                mkdir -p $out/bin $out/share/pico-386
                cp dos/MAIN.EXE $out/bin/pico386.exe
                cp dos/MAIN.EXE $out/share/pico-386/MAIN.EXE
                runHook postInstall
              '';
            };

            tools = pkgs.buildEnv {
              name = "pico-386-tools";
              paths = tools;
            };
          };

          devShells.default = pkgs.mkShell {
            nativeBuildInputs = tools;
            WATCOM = "${pkgs.open-watcom-bin}";
          };
        };
    };
}
