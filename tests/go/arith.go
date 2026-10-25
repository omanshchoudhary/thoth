package main

import "fmt"

func main() {
	a := 17
	b := 5
	fmt.Println(a+b, a-b, a*b, a/b, a%b)
	fmt.Println(-a/b, -a%b, a/-b, a%-b)
	fmt.Println(1+2*3, (1+2)*3, 10-4-3, 100/10/2, 2*3%4)
	fmt.Println(-(-a), - -b)
	big := 9223372036854775807
	big = big + 1
	fmt.Println(big)
	small := -9223372036854775807 - 1
	minus := -1
	fmt.Println(small/minus, small%minus)
	x := 1000000
	fmt.Println(x * x * x)
	var y int
	fmt.Println(y)
}
