{
  description = "A very basic flake";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      pkgs = import nixpkgs { system = "x86_64-linux"; };
    in
    {

      packages.x86_64-linux.meowmsgclient = pkgs.callPackage (
        {
          clangStdenv,
          ncurses,
          cjson,
          curl,
          openssl,
        }:
        clangStdenv.mkDerivation {
          pname = "meowmsgclient";
          version = "0.1.0";
          src = ./.;
          buildInputs = [
            ncurses
            cjson
            curl
            openssl,
          ];

          installPhase = ''
            runHook preInstall

            mkdir -p $out/bin
            cp meowmsgclient $out/bin

            runHook postInstall
          '';
        }
      ) { };

    };
}
