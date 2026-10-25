package main

import "fmt"

func divide(a int, b int) int {
	return a / b
}

func main() {
	fmt.Println(divide(10, 2))
	fmt.Println(divide(1, 0))
}
