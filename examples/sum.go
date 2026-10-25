package main

import "fmt"

func main() {
	n := 5
	sum := 0
	for n > 0 {
		sum += n
		n--
	}
	fmt.Println(sum)
}
