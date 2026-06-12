#include "Execution/Task.hpp"

using namespace Execution;

int main() {
    Task self = Task::current();
    Task parentTask = self.parent();
    self.send(parentTask, "Hello from guest!");
    return 0;
}
