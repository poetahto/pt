/* 
    ptm_map - v0.01 - public domain `.map` loader
                            No warranty implied; use at your own risk

    Do this:
      #define PT_MAP_IMPLEMENTATION
    before you include this file in *one* C or C++ file to 
    create the implementation.

    BASIC USAGE:
      See ptm_demo.c for example.

    OPTIONS:
      #define PTM_ASSERT(expr)
        change how assertions are enforced

      #define PTM_ACREATE/APUSH/AFREE
        change arena allocation strategy

      #define PTM_HASH/CREATE_HASH
        change hash type/strategy

      #define PTM_REAL <float|double|custom>
        change precision of all real numbers (default to double)

      #define PTM_STRTOR
        re-define to change how real numbers are parsed, defaults 
        to strtod/strtof. this can be a bottleneck: consider 
        replacing for better performance

    BACKGROUND:
      ".map" files define brush-based levels for games in a simple, 
      plaintext format. They were originally used in the first quake 
      game, and later modified for its descendants (half-life, ect.)

      While brush-based level design has fallen out of favor in many 
      games, it remains a powerful prototyping and blockout tool, 
      along with having a retro aesthetic. There are also powerful 
      ".map" editors that are very mature, such as Trenchbroom.

      A map file is structured to not just define the visual layout of 
      a scene, but also the gameplay properties. Indeed, a map file is 
      nothing more than a list of entities which may or may not be 
      associated with a mesh (think: lights, player spawn positions, 
      ambient noise emitters). These are all defined by key-value 
      pairs which can be interpreted by the engine.

      The contents of a map file must be processed in several steps
      before they can be rendered in a game.

