import React, { useState, useRef, useEffect } from 'react';
import Header from '../components/Header';
import SiteFooter from '../components/SiteFooter';
import { ArrowRight, ChevronDown } from 'lucide-react';
import { useNavigate } from 'react-router-dom';
import ImageComparison from '../components/ImageComparison';
import './LandingPage.css';

export default function LandingPage() {
  const [isDropdownOpen, setIsDropdownOpen] = useState(false);
  const [openFaq, setOpenFaq] = useState(null);
  const dropdownRef = useRef(null);
  const navigate = useNavigate();

  const faqs = [
    { q: 'Как открыть меню чита?', a: 'Нажмите клавишу Insert на клавиатуре для открытия/закрытия меню.' },
    { q: 'Как установить и запустить?', a: '1. Запустите лоадер от имени администратора. 2. Авторизуйтесь. 3. Нажмите Inject и дождитесь конца загрузки драйвера. 4. Запустите игру и зайдите на сервер. 5. Нажмите Launch — меню появится в игре.' },
    { q: 'Ошибка "Failed to load driver"', a: '1. Убедитесь, что Изоляция ядра (Core Isolation → Целостность памяти) отключена.\n2. Перезагрузите ПК после изменений.\n3. На Windows 11 25H2 выполните в CMD от администратора:\nbcdedit /set hypervisorlaunchtype off\nи перезагрузите ПК.\n4. Убедитесь, что Windows Defender выключен.\n5. Запускайте лоадер ДО запуска игры — если игра уже запущена, драйвер может быть заблокирован.\n6. Попробуйте отключить или удалить другие античиты (например, Faceit AC, Riot Vanguard), так как они могут блокировать загрузку драйвера.' },
    { q: 'Что будет, если выйдет обновление сервера?', a: 'Наша команда оперативно обновляет софт под новые патчи, обычно это занимает от нескольких часов до суток.' },
    { q: 'Безопасно ли использовать?', a: 'Да, чит работает через kernel-level драйвер и не внедряется в процесс игры. External подход + streamproof обеспечивают максимальную безопасность.' },
    { q: 'Как получить техническую поддержку?', a: 'Вы можете обратиться напрямую к владельцу - t.me/fraud444' },
  ];

  useEffect(() => {
    function handleClickOutside(event) {
      if (dropdownRef.current && !dropdownRef.current.contains(event.target)) {
        setIsDropdownOpen(false);
      }
    }
    document.addEventListener('mousedown', handleClickOutside);
    return () => document.removeEventListener('mousedown', handleClickOutside);
  }, []);

  useEffect(() => {
    if (window.location.hash === '#faq') {
      document.getElementById('faq')?.scrollIntoView();
    }
  }, []);

  return (
    <div className="landing-page">
      <Header />

      <section className="split split--hero">
        <div className="split__copy">
          <h1 className="hero-title">Ваш центр<br />возможностей.</h1>
          <p className="hero-subtitle">
            Приватные внешние модификации для GTA V. Максимальная безопасность и регулярные обновления.
          </p>
          <div className="hero-actions" ref={dropdownRef}>
            <button
              type="button"
              className="cta-outline"
              aria-expanded={isDropdownOpen}
              onClick={() => setIsDropdownOpen(!isDropdownOpen)}
            >
              Наши продукты <ArrowRight size={16} aria-hidden="true" />
            </button>
            {isDropdownOpen && (
              <div className="hero-products-dropdown products-dropdown">
                <button type="button" className="product-dropdown-card" onClick={() => navigate('/games/external')}>
                  <span className="product-icon"><img src="/external.png" className="product-icon-img" alt="" /></span>
                  <span className="product-name">EXTERNAL</span>
                  <span className="product-status">Undetected</span>
                </button>
                <button type="button" className="product-dropdown-card" onClick={() => navigate('/games/trigger')}>
                  <span className="product-icon"><img src="/external.png" className="product-icon-img" alt="" /></span>
                  <span className="product-name">TRIGGER</span>
                  <span className="product-status">Undetected</span>
                </button>
              </div>
            )}
          </div>
        </div>
        <figure className="split__proof">
          <img src="/macbook.png" alt="Интерфейс продукта на экране" />
        </figure>
      </section>

      <section className="split split--reverse">
        <figure className="split__proof split__proof--frame">
          <ImageComparison
            beforeImage="/comparison-without.png"
            afterImage="/comparison-with.png"
          />
        </figure>
        <div className="split__copy">
          <header className="head-hang">
            <h2>Скрытие чита на записи</h2>
          </header>
          <p className="split__lede">OBS, Nvidia, Discord and Other</p>
        </div>
      </section>

      <section className="split">
        <div className="split__copy">
          <header className="head-hang">
            <h2>Безопасность</h2>
          </header>
          <p className="split__lede">0 банов за более чем 6 месяцев тестов external продукта.</p>
        </div>
        <figure className="split__proof">
          <img src="/image 39.svg" alt="" />
        </figure>
      </section>

      <section className="split split--reverse">
        <figure className="split__proof split__proof--shot">
          <img src="/11111.png" alt="Настройки кастомизации интерфейса" />
        </figure>
        <div className="split__copy">
          <header className="head-hang">
            <h2>Почувствуй разницу</h2>
          </header>
          <p className="split__lede">
            Полная кастомизация и удобство для каждого элемента, фактор удобства продукта превыше всего.
          </p>
        </div>
      </section>

      <section className="faq-section" id="faq">
        <header className="head-hang">
          <h2 className="faq-main-title">Часто задаваемые вопросы</h2>
        </header>
        <div className="faq-list">
          {faqs.map((faq, index) => (
            <div
              key={index}
              className={`faq-item ${openFaq === index ? 'active' : ''}`}
            >
              <button
                type="button"
                className="faq-header"
                aria-expanded={openFaq === index}
                onClick={() => setOpenFaq(openFaq === index ? null : index)}
              >
                <h3 className="faq-question">{faq.q}</h3>
                <ChevronDown className="faq-icon" size={18} aria-hidden="true" />
              </button>
              <div className="faq-answer-wrapper">
                <div className="faq-answer">
                  <p className="faq-answer-inner">{faq.a}</p>
                </div>
              </div>
            </div>
          ))}
        </div>
      </section>

      <SiteFooter />
    </div>
  );
}
