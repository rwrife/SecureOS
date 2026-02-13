__attribute__((used))
void kmain(void) {
  volatile const char *marker = "KMAIN_REACHED";
  (void)marker;
  for (;;) {
    __asm__ __volatile__("hlt");
  }
}
