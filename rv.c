// RV32I program.

void putInt(int n);
void putStr(char* str);
int getInt();

int main() {
  char enter_msg[] = "Enter the number of terms:";
  putStr(enter_msg);
  int n = getInt();

  char start_msg[] = "Fibonacci sequence:";
  putStr(start_msg);
  if (n == 0) return 0;
  putInt(1);
  if (n == 1) return 0;
  putInt(1);
  if (n == 2) return 0;
  int n1 = 1, n2 = 1, n3;
  for (int i = 2; i < n; ++i) {
    n3 = n1 + n2;
    putInt(n3);
    n1 = n2;
    n2 = n3;
  }

  return 0;
}

void putInt(int n) {
  asm volatile(
      "li a0, 100\n"
      "mv a1, %0\n"
      "ecall\n"
      :
      : "r"(n)
      : "a0", "a1");
}

void putStr(char* str) {
  asm volatile(
      "li a0, 101\n"
      "mv a1, %0\n"
      "ecall\n"
      :
      : "r"(str)
      : "a0", "a1");
}

int getInt() {
  int result;
  asm volatile(
      "ebreak\n"
      "li a0, 200\n"
      "ecall\n"
      "mv %0, a0\n"
      : "=r"(result)
      :
      : "a0");
  return result;
}
