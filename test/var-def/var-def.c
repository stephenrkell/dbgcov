int cond(int n);
void read(int x);

int example(int n) {
  int delayed;
  int init = 0; // declaration with initialiser
  int a, b;
  if (cond(n)) {
    int init = 2; // variable shadowing
    a = 1; // initialised from multiple line block
    read(a);
  }
  if (cond(-n))
    b = -1; // initialised from single line block
  delayed = 2; // definition after declaration
  for (int i = 0; i < n; i++) { // block header definition
    delayed++;
  }
  return a + b;
}
