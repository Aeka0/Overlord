#include "reload_item_attachment_tests.hpp"
#include <iostream>

int main()
{
	int checks{},failures{};
	reload_item_attachment_tests::run([&](bool value,const char* message)
	{
		++checks;if(!value){++failures;std::cerr<<"FAIL "<<message<<'\n';}
	});
	std::cout<<"attachment poses checks="<<checks<<" failures="<<failures<<'\n';
	return failures?1:0;
}
