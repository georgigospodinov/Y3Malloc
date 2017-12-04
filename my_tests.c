#include "myalloc.h"
#include <stdio.h>
#include <assert.h>

int main() {

    printf("Initial Allocation... \n");
    int size = 17*sizeof(char);
    char* characters = myalloc(size);
    printf("characters=%p\n", characters);

    characters[6] = 'c';
    assert(characters[6] == 'c');

    characters = myrealloc(characters, 1024*sizeof(char));
    printf("characters=%p\n", characters);
    assert(characters[6] == 'c');

    characters = myrealloc(characters, 7*sizeof(char));
    printf("characters=%p\n", characters);
    assert(characters[6] == 'c');

    myfree(characters);

    int* numbers = mycalloc(10, sizeof(int));
    numbers[9] = 12;
    numbers[0] = 1;
    numbers = myrealloc(numbers, 50*sizeof(int));
    assert(numbers[0] == 1);
    assert(numbers[9] == 12);

    return 0;
}
