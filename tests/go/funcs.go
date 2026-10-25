package main

import "fmt"

func main() {
	fmt.Println(fib(20), fact(10))
	fmt.Println(sum6(1, 2, 3, 4, 5, 6))
	greet(3)
	fmt.Println(gcd(1071, 462), power(2, 10))
	fmt.Println(isEven(10), isEven(7))
}

func fib(n int) int {
	if n < 2 {
		return n
	}
	return fib(n-1) + fib(n-2)
}

func fact(n int) int {
	result := 1
	for i := 2; i <= n; i++ {
		result *= i
	}
	return result
}

func sum6(a, b, c int, d int, e, f int) int {
	return a + b*10 + c*100 + d*1000 + e*10000 + f*100000
}

func greet(times int) {
	for i := 0; i < times; i++ {
		fmt.Println(i)
	}
}

func gcd(a int, b int) int {
	for b != 0 {
		next := a % b
		a = b
		b = next
	}
	return a
}

func power(base int, exp int) int {
	if exp == 0 {
		return 1
	}
	half := power(base, exp/2)
	if exp%2 == 0 {
		return half * half
	}
	return half * half * base
}

func isEven(n int) bool {
	if n == 0 {
		return true
	}
	return isOdd(n - 1)
}

func isOdd(n int) bool {
	if n == 0 {
		return false
	}
	return isEven(n - 1)
}
