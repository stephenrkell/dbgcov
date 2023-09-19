int cond(int n);
void read(int x);

int example(
  int n // parameter
) {
  int delayed, unstructured;
  int init = 0; // declaration with initialiser
  int a, b;
  if (cond(n)) {
    int init = 2; // variable shadowing
    a = 1; // initialised from multiple line block
    read(a);
  } else {
    a = 2; // TODO: CFG analysis would show `a` as defined for all paths
  }

  goto undefined;
definition:
  unstructured = a + n; // TODO: unstructured definition out of source line order
  goto defined;
undefined:

  if (cond(-n))
    b = -1; // initialised from single line block
  else
    b = 0; // TODO: CFG analysis would show `b` as defined for all paths
  delayed = 2; // definition after declaration
  for (int i = 0; i < n; i++) { // block header definition
    delayed++;
  }

  goto definition;
defined:

  return a + b + delayed + unstructured;
}
