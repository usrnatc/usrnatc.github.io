var app = document.getElementById('app');

var typewriter = new Typewriter(app, {
        loop: false,
        delay: 75,
        autoStart: true,
        cursor: '_',
        strings: ['Nathan Corcoran links hub ...<br />I am testing this out.']
});

typewriter
    .deleteAll(1)
    .pauseFor(50)
    .deleteAll(1)
    .typeString('I am currently a third year software engineering student at UQ. See my resume linked at the top of the page for more info about me.')
    .pauseFor(300)
    .deleteAll(1)
    .typeString('See my github: <br />')
    .pauseFor(300)
    .typeString('<a href="https://github.com/usrnatc">Github repos</a>')
