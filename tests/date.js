
//
// Some sample code using the mocha date object.
// 
// WARNING: dates before 1970 will crash
//
// To run this script: datetime sample.js
//

today = new Date();
FCS = new Date("March 18, 1996");

if ( today.getTime() < FCS.getTime() ) {
    print("It's not FCS yet. Be patient!");
}

if ( today.getYear()  == FCS.getYear()  &&
     today.getMonth() == FCS.getMonth() &&
     today.getDate()  == FCS.getDate()  ) {
   print("It's FCS day, you shouldn't be looking at date/time functions!");
}

numberOfMilliSecondsPerDay = 24 * 60 * 60 * 1000 ;

daysLeft = (FCS.getTime()-today.getTime()) / numberOfMilliSecondsPerDay;

print("You have " + Math.round(daysLeft) + " development days left");
print("Today is " + today.toString() );
print("FCS day is " + FCS.toString() );

// there are other ways to construct a date object

x = new Date("December 25, (Christmas) 1995"); // comments are in parens
print(x.toString());

x = new Date(95,11,25);
print(x.toString());

x = new Date(95,11,25,9,30,0);
print(x.toString());

s = "Dec 25,1995 gmt";
n = Date.parse(s);
x = new Date(n);
print(x.toString());

// here are most of the methods 

print(x.getYear());    // since 1900
print(x.getMonth());   // 0-11
print(x.getDate());    // 1-31
print(x.getHours());   // 0-23
print(x.getMinutes()); // 0-59
print(x.getSeconds()); // 0-59

print(x.getDay()); /* day of the week, Sunday is 0 */

x.setYear(95);
x.setMonth(11);
x.setDate(25);
x.setHours(11);
x.setMinutes(42);
x.setSeconds(1);
print(x.toString());

print(x.getTime()); // milliseconds since unix epoch

