var app = document.getElementById('app');

var typewriter = new Typewriter(app, {
        loop: false,
        delay: 75,
        autoStart: true,
        cursor: '_',
        strings: ['Nathan Corcoran links hub<br />I am testing this out.<br /><br />']
});

typewriter
    .pauseFor(50)
    .typeString('#include "me.h"<br />')
    .typeString('Person me = (Person) malloc(sizeof(Person));<br />')
    .typeString('me.age = 20;<br />')
    .typeString('me.uni = (char *) malloc(BUFFERSIZE * sizeof(char));<br />')
    .typeString('me.uni = \"University of Queensland\\n\";<br />')
    .typeString('me.langs = {\"Python\", \"Java\", \"C\", \"Haskell\", \"Dafny\", \"HTML\", \"CSS\"};<br />')
    .typeString('printf("%s\\n", me.info);<br />')
    .typeString('$ I am currently a third year software engineering student at UQ. See my resume through the \'/about\' link at the top of the page for more info about me.<br /><br />')
    .pauseFor(800)
    .typeString('free_all_memory(&me);<br />')
    .deleteAll(5)
    .typeString('See my github: <br />')
    .pauseFor(300)
    .typeString('<a href="https://github.com/usrnatc">Github repos</a>')
