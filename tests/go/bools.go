package main

import "fmt"

func check(label int, value bool) bool {
	fmt.Println(label, value)
	return value
}

func main() {
	t := true
	f := false
	fmt.Println(t, f, !t, !f)
	fmt.Println(t && f, t || f, t == f, t != f)
	fmt.Println(3 < 4, 3 > 4, 4 <= 4, 5 >= 6, 2 == 2, 2 != 2)
	result := check(1, false) && check(2, true)
	fmt.Println(result)
	result = check(3, true) || check(4, true)
	fmt.Println(result)
	result = check(5, true) && check(6, false) || check(7, true)
	fmt.Println(result)
	var flag bool
	fmt.Println(flag)
	flag = 1 < 2 && !(2 < 1)
	fmt.Println(flag)
	if !flag || t == false {
		fmt.Println(0)
	} else {
		fmt.Println(1)
	}
}
