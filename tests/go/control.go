package main

import "fmt"

func classify(n int) int {
	if n < 0 {
		return -1
	} else if n == 0 {
		return 0
	} else {
		return 1
	}
}

func main() {
	for i := 0; i < 5; i++ {
		fmt.Println(i, i*i)
	}
	n := 3
	for n > 0 {
		fmt.Println(n)
		n--
	}
	count := 0
	for {
		count++
		if count == 4 {
			break
		}
	}
	fmt.Println(count)
	odd := 0
	for i := 0; i < 10; i++ {
		if i%2 == 0 {
			continue
		}
		odd += i
	}
	fmt.Println(odd)
	total := 0
	for i := 1; i <= 3; i++ {
		for j := 1; j <= 3; j++ {
			if j > i {
				break
			}
			total += i * j
		}
	}
	fmt.Println(total)
	if v := classify(-7); v < 0 {
		fmt.Println(v, true)
	}
	fmt.Println(classify(0), classify(42))
	for ; n < 3; {
		n++
	}
	fmt.Println(n)
}
