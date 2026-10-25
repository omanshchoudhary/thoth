package main

import "fmt"

func id(n int) int {
	return n
}

func mix(a int, b int, c int) int {
	return a*100 + b*10 + c
}

func main() {
	a := 1
	b := 2
	c := 3
	d := 4
	e := 5
	f := 6
	g := 7
	h := 8
	r := (a+b)*(c+d) - (e+f)*(g+h) + (a*h - b*g + c*f - d*e)
	fmt.Println(r)
	s := a + b + c + d + e + f + g + h
	t := a*b + c*d + e*f + g*h
	fmt.Println(s, t, s*t-r)
	fmt.Println(id(a)+id(b)*id(c), mix(id(d), e+f, g*h-50))
	acc := 0
	for i := 0; i < 50; i++ {
		acc = acc + (i*a+b)*(c-i%d) - e*(f+g)/(h+i)
	}
	fmt.Println(acc)
}
