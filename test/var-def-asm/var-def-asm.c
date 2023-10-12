// Extracted from FLINT's `nmod_vec.h` and related files

typedef unsigned long int mp_limb_t;

#define add_ssaaaa(sh, sl, ah, al, bh, bl)                                     \
  __asm__("addq %5,%q1\n\tadcq %3,%q0"                                         \
          : "=r"(sh), "=&r"(sl)                                                \
          : "0"((mp_limb_t)(ah)), "rme"((mp_limb_t)(bh)),                      \
            "%1"((mp_limb_t)(al)), "rme"((mp_limb_t)(bl)))

void example() {
  mp_limb_t s0, s1, s2, t0, t1;
  add_ssaaaa(s1, s0, s1, s0, 0, t0);
}
