int example() {
  static int bob;     // static locals are implicitly empty initialised
                      // _but_ that happens outside of the function itself,
                      // so there's no computation here.
  static int tom = 1; // even with an explicit initialiser,
                      // this work still happens outside of the function,
                      // so there's still no computation.
  bob++;
  tom++;
  return bob + tom;
}
