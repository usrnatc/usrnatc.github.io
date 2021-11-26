var app = document.getElementById('app');

var typewriter = new Typewriter(app, {
        loop: false,
        delay: 75,
        autoStart: true,
        cursor: '_',
	strings: ['']
});

typewriter
    .typeString('#include "me.h"<br />')
    .typeString('Person me = (Person *) malloc(sizeof(Person));<br />')
    .typeString('me->age = 20;<br />')
    .typeString('me->uni = (char *) malloc(BUFFERSIZE * sizeof(char));<br />')
    .typeString('sprintf(me->uni, \"University of Queensland\\n\");<br />')
    .typeString('me->langs = {\"Python\", \"Java\", \"C\", \"Haskell\", \"Dafny\", \"HTML\", \"CSS\"};<br />')
    .typeString('printf("%s\\n", me->info);<br />')
    .typeString('$ I am currently a third year software engineering student at UQ.<br />\
		  &nbsp;&nbsp;I made this web-page to learn some simple front-end abailities with HTML, CSS, and trivial javascript.<br />\
		  &nbsp;&nbsp;If you have any queries, concerns, and/or improvements to this web-page let me know <br />&nbsp;&nbsp;in an email through the contact link at the top of the page.<br /><br />\
		  &nbsp;&nbsp;See my resume through the \'/about\' link at the top of the page for more info about me.<br /><br />')
