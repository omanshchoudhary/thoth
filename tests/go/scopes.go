package main

import "fmt"

func main() {
	x := 1
	{
		x := 2
		fmt.Println(x)
		{
			x := 3
			fmt.Println(x)
		}
		fmt.Println(x)
	}
	fmt.Println(x)
	var a int
	var b = 7
	var c int = 8
	a += 10
	a -= 3
	a *= 4
	a /= 2
	a %= 5
	b++
	c--
	fmt.Println(a, b, c)
	for i := 0; i < 2; i++ {
		x := i * 100
		fmt.Println(x)
	}
	if x := 50; x > 10 {
		fmt.Println(x)
	}
	fmt.Println(x)
}
