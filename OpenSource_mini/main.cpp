//
//  main.cpp
//  OpenSource_mini
//
//  Created by Ivan Avdeev on 05.11.12.
//
//

#include <Kapusha/sys/runGlut.h>
#include "../OpenSource/OpenSource.h"

int main(int argc, const char * argv[])
{
  return runGlut(argc, argv, new OpenSource);
}

