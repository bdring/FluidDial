#ifndef ARDUINO
#    include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>
#    if defined(SDL_h_)

extern void setup();
extern void loop();

char* comname;
int   main(int argc, char** argv) {
    if (argc != 2) {
        printf("Usage: %s COMn\n", argv[0]);
        exit(1);
    }
    comname = argv[1];

    setup();

    while (1) {
        loop();
    }
    return 0;
}
#    endif
#endif
