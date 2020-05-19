#include <iostream>

int main(int argc, char *argv[]) {
  std::cout << "What's your name?" << std::endl;
  std::string name;
  std::cin >> name;

  if (name == "root")
    std::cout << "What is your command?" << std::endl;
  else
    std::cout << "Hello, " << name << "!" << std::endl;

  return 0;
}
