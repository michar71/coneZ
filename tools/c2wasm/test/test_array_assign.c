/* Test array element assignment */

int arr[3];

int setup() {
    /* Test array element assignment */
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    
    /* Test reading back */
    int sum = arr[0] + arr[1] + arr[2];
    
    return sum;
}

void loop() {
}
