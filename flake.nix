{
  description = "pebble_icebreaker - a cyberpunk lockpicking game for the Pebble Time 2";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    pebble.url = "github:pebble-dev/pebble.nix";
  };

  outputs = { self, nixpkgs, flake-utils, pebble, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        # The emulator is built against pebble.nix's nixpkgs, so its SDL has to come from
        # there too, or the two disagree about glibc.
        pebblePkgs = pebble.inputs.nixpkgs.legacyPackages.${system};
      in
      {
        devShells.default = pebble.pebbleEnv.${system} {
          packages = with pkgs; [
            gnumake
          ];

          # The emulator's SDL2 is sdl2-compat, which loads SDL3 at runtime. The path is
          # replaced rather than extended: libraries inherited from another shell can be
          # built against a newer glibc than the emulator's and stop SDL3 loading.
          shellHook = ''
            export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath [ pebblePkgs.sdl3 ]}"
            export PEBBLE_EMULATOR='emery'
            echo "Pebble dev environment loaded. Use 'pebble build' to compile."
          '';
        };
      });
}
