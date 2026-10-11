// wrapper: Arduino compila i .c della root, non quelli nelle sottocartelle.
// Un wrapper per file tiene ogni sorgente in una UNITA' DI COMPILAZIONE
// SEPARATA: pic.c/pit.c/memory.c definiscono tutti funzioni static con gli
// stessi nomi (in, out, install, name), quindi un'amalgamazione unica NON
// compila. Gli #include "..." dentro il file incluso si risolvono rispetto
// alla sua cartella (cpus/vxt/), quindi non serve altro.
#define VXT_NO_LOG
#ifndef NDEBUG
  #define NDEBUG
#endif
#include "cpus/vxt/dummy.c"
