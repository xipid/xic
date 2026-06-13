#include "Task/Task.hpp"


int main() {
    Task::Task self = Task::Task::current();
    Task::Task parentTask = self.parent();
    self.send(parentTask, "Hello from guest!");
    return 0;
}
