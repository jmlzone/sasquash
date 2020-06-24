String style_js = "        <style>\n\
            body {\n\
                font-family: Arial,Helvetica,sans-serif;\n\
                background: #181818;\n\
                color: #EFEFEF;\n\
                font-size: 16px;\n\
            }\n\
            .input-group>label {\n\
                display: inline-block;\n\
                padding-right: 10px;\n\
                min-width: 47%;\n\
            }\n\
            .input-group input,.input-group select {\n\
                flex-grow: 1;\n\
            }\n\
            .range-max,.range-min {\n\
                display: inline-block;\n\
                padding: 0 5px\n\
            }\n\
            .hidden {\n\
                display: none\n\
            }\n\
            .switch {\n\
                display: block;\n\
                position: relative;\n\
                line-height: 22px;\n\
                font-size: 16px;\n\
                height: 22px;\n\
            }\n\
            .switch input {\n\
                outline: 0;\n\
                opacity: 0;\n\
                width: 0;\n\
                height: 0;\n\
            }\n\
            .slider {\n\
                width: 50px;\n\
                height: 22px;\n\
                border-radius: 22px;\n\
                cursor: pointer;\n\
                background-color: grey;\n\
            }\n\
            .slider,.slider:before {\n\
                display: inline-block;\n\
                transition: .4s;\n\
            }\n\
            .slider:before {\n\
                position: relative;\n\
                content: \"\";\n\
                border-radius: 50%;\n\
                height: 16px;\n\
                width: 16px;\n\
                left: 4px;\n\
                top: 3px;\n\
                background-color: white;\n\
            }\n\
            input:checked+.slider {\n\
                background-color: #ff3034;\n\
            }\n\
            input:checked+.slider:before {\n\
                -webkit-transform: translateX(26px);\n\
                transform: translateX(26px);\n\
            }\n\
	    a {\n\
		background-color: blue;\n\
		color: white;\n\
		text-align: center;\n\
		display: block;\n\
		padding: 0.5em 1.5em;\n\
		margin-top: 1em;\n\
		margin-bottom: 1em;\n\
		line-height: 3.5;\n\
		text-decoration: none;\n\
		text-transform: none;\n\
            }\n\
       </style>\n";

String script_js = "       <script>\n\
	function clickColor(post) {\n\
	    var c, r,g,b, on, mode, all, num, m;\n\
	    on = document.getElementById(\"on\").checked;\n\
	    all = document.getElementById(\"all\").checked;\n\
	    num = document.getElementById(\"num\").value;\n\
	    mode = document.getElementById(\"mode\").value;\n\
	    if(mode==\"Constant\") {\n\
		document.getElementById(\"allDIV\").classList.remove('hidden');\n\
		document.getElementById(\"colorDIV\").classList.remove('hidden');\n\
		if(all==true) {\n\
		    document.getElementById(\"numDIV\").classList.add('hidden');\n\
		} else {\n\
		    document.getElementById(\"numDIV\").classList.remove('hidden');\n\
		}\n\
	    } else {\n\
		document.getElementById(\"allDIV\").classList.add('hidden');\n\
		document.getElementById(\"numDIV\").classList.add('hidden');\n\
		document.getElementById(\"colorDIV\").classList.add('hidden');\n\
	    }\n\
	    c = document.getElementById(\"html5colorpicker\").value;\n\
	    r = parseInt(c.substr(1,2), 16);\n\
	    g = parseInt(c.substr(3,2), 16);\n\
	    b = parseInt(c.substr(5,2), 16);\n\
	    m=\"color r=\" + r + \"&b=\" + b + \"&g=\" + g + \"&on=\" + on + \"&all=\" + all + \"&num=\" + num + \"&mode=\" + mode;\n\
      console.log(\"query is: \", m);\n\
 	    var query = `/setcolor?${m}`\n\
 	    if(post) { \n\
 	      fetch(query, {method: 'post'});\n\
 	    }\n\
 	}\n\
       </script>\n";


String control_html = "	    <div class=\"input-group\" id=\"onDIV\">\n\
  	      <label for=\"on\">light on/off</label>\n\
	      <div class=\"switch\">\n\
		<input type=checkbox id=on onchange=clickColor(true) ";
String control_html1 = ">\n\
		<label class=\"slider\" for=\"on\"></label>\n\
	      </div>\n\
	    </div>\n\
	    <div class=\"input-group\" id=\"modeDIV\">\n\
	      <label for=\"mode\">Mode<br></label>\n\
	      <select id=\"mode\" name=\"mode\" onchange=clickColor(true)>\n";
String control_html2 = " 	      </select><br>\n\
	    </div>\n\
	    <div class=\"input-group\" id=\"colorDIV\">\n\
              <h3>Pick Color</h3>\n\
              <input type=\"color\" id=\"html5colorpicker\" onchange=\"clickColor(true)\" value=\"#ff0000\" style=\"width:85%;\">\n\
	    </div>\n\
	    <div class=\"input-group\" id=\"allDIV\">\n\
  	      <label for=\"all\">All Same Color</label>\n\
	      <input type=checkbox id=all onchange=clickColor(true)>\n\
	    </div>\n\
	    <div class=\"input-group\" id=\"numDIV\">\n\
  	      <label for=num>Led Number</label>\n\
	      <input id=num type=number min=1 max=";
String control_html3 = " value=1 onchange=clickColor(true)>\n\
	    </div>\n";

String tz_form = "		    <div class=\"input-group\" id=\"dtDIV\">\n\
	      <label for=\"tz\">Timezone<br></label>\n\
	      <select id=\"tz\" name=\"tz\">\n\
    <option value=\"EDT4\">Eastern Daylight</option>\n\
    <option value=\"EST5\">Eastern Standard</option>\n\
		<option value=\"CST6CDT\">Central</option>\n\
		<option value=\"MST7MDT\">Mountain</option>\n\
		<option value=\"PST8PDT\">Pacific</option>\n\
	      </select>\n\
  	    </div>\n";
  
String footer_links = "	    <a href =\"/\">Color setting and control</a><br>\n\
	    <a href =\"/configLed\">Configure and save LED configuration</a><br>\n\
	    <a href =\"/configTime\">Timezone and timer setting</a><br>\n\
	    <a href =\"/configWifi\">Network settings</a> <br>\n";
